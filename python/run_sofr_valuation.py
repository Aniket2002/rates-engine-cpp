"""Replay captured fixings and imported OIS quotes into audited SOFR cash flows and risk."""
import argparse
import csv
from datetime import timedelta
import json
from pathlib import Path
import sys

import sofr_market_data as md


def run(manifest_path, module_dir):
    sys.path.insert(0, str(Path(module_dir).resolve()))
    import pyrates as r
    manifest_path = Path(manifest_path)
    manifest = md.decode(manifest_path.read_bytes())
    as_of = md.timestamp(manifest["as_of"])
    paths = {key: manifest_path.parent / manifest[key] for key in ("calendar", "fixings", "quotes", "trade")}
    calendar = md.Calendar(md.decode(paths["calendar"].read_bytes()))
    fixing_snapshot = md.read_snapshot(paths["fixings"], "sofr-fixings")
    quote_snapshot = md.read_snapshot(paths["quotes"], "ois-quotes")
    if fixing_snapshot["calendar_sha256"] != calendar.sha256:
        raise md.DataError("Fixing/calendar mismatch")
    if md.timestamp(fixing_snapshot["received_at"]) > as_of:
        raise md.DataError("Fixing capture was unavailable at valuation time")
    quotes = md.quotes_as_of(quote_snapshot, calendar, as_of)
    trade = md.decode(paths["trade"].read_bytes())
    if trade.get("currency") != "USD" or trade.get("index") != "SOFR" or trade.get("side") not in ("payer", "receiver"):
        raise md.DataError("Expected USD SOFR trade with explicit payer/receiver side")
    rc = r.Calendar({r.Date(d.isoformat()) for d in calendar.holidays})

    def cpp_conventions(document):
        md.conventions(document)
        result = r.SofrConventions()
        for name, value in document.items():
            setattr(result, name, value)
        result.calendar = result.payment_calendar = rc
        return result

    # Conservative coverage guard includes lookbacks, spot lag and final payment lag.
    first = min(md.day(trade["effective"]), as_of.date())-timedelta(days=120)
    months = max(q["tenor_months"] for q in quotes["quotes"])
    quote_end = md.day(str(r.Date(as_of.date().isoformat()).add_months(months)))
    last = max(md.day(trade["maturity"]), quote_end)+timedelta(days=120)
    calendar.business(first)
    calendar.business(last)
    records = [r.FixingRecord(r.Date(row["date"]), r.Timestamp(row["available_at"]), row["rate"],
                             row["source"], row["revision"]) for row in fixing_snapshot["records"]]
    fixings = r.FixingStore(records)
    ts = r.Timestamp(manifest["as_of"])
    c = cpp_conventions(quotes["conventions"])
    builder = r.SofrCurveBootstrapper(c)
    market = [r.SofrQuote(q["label"], q["tenor_months"], q["rate"]) for q in quotes["quotes"]]
    calibrated = builder.build(ts, market, fixings)
    projection = calibrated.curve
    projection_calibration = None
    projection_snapshot = None
    if "projection_quotes" in manifest:
        paths["projection_quotes"] = manifest_path.parent / manifest["projection_quotes"]
        projection_snapshot = md.read_snapshot(paths["projection_quotes"], "ois-quotes")
        forward_doc = md.quotes_as_of(projection_snapshot, calendar, as_of)
        forward_builder = r.SofrCurveBootstrapper(cpp_conventions(forward_doc["conventions"]))
        forward_quotes = [r.SofrQuote(q["label"], q["tenor_months"], q["rate"]) for q in forward_doc["quotes"]]
        forward_months = max(q["tenor_months"] for q in forward_doc["quotes"])
        calendar.business(md.day(str(r.Date(as_of.date().isoformat()).add_months(forward_months)))+timedelta(days=120))
        projection_calibration = forward_builder.build_forward(calibrated.curve, ts, forward_quotes, fixings)
        projection = projection_calibration.curve
    swap = r.SofrSwap(r.Date(trade["effective"]), r.Date(trade["maturity"]),
                     md.number(trade["notional"], "notional"), md.number(trade["fixed_rate"], "fixed_rate"),
                     r.Side.Payer if trade["side"] == "payer" else r.Side.Receiver,
                     cpp_conventions(trade["conventions"]))
    # Report and risk use the same end-of-day payment exclusion convention.
    value = r.price_sofr(calibrated.curve, projection, swap, fixings, ts)
    risk = r.sofr_curve_risk(calibrated.curve, projection, swap, fixings, ts)
    cashflows, observations = [], []
    for i, cf in enumerate(value.coupons):
        cashflows.append(dict(coupon=i, start=str(cf.period.start), end=str(cf.period.end),
                              payment=str(cf.period.payment), fixed_amount=cf.fixed_amount,
                              floating_amount=cf.floating_amount, discount=cf.discount,
                              fixed_accrued=cf.fixed_accrued, floating_accrued=cf.floating_accrued))
        for obs in cf.observations:
            observations.append(dict(coupon=i, accrual_start=str(obs.accrual_start), accrual_end=str(obs.accrual_end),
                                     fixing_date=str(obs.fixing_date), observation_end=str(obs.observation_end),
                                     weight=obs.weight, rate=obs.rate, realized=obs.realized))
    def buckets(items):
        return [dict(label=x.label, pillar=str(x.maturity), dv01=x.dv01) for x in items]
    def calibration(result):
        return [dict(effective=str(x.effective), pillar=str(x.pillar), model_quote=x.model_quote, residual=x.residual)
                for x in result.diagnostics]
    report = dict(schema_version=1, as_of=manifest["as_of"],
                  synthetic_quotes=quotes["synthetic"] or bool(projection_snapshot and forward_doc["synthetic"]),
                  fixing_source=fixing_snapshot["source"], quote_source=quotes["source"],
                  fixing_availability_policy=fixing_snapshot["availability_policy"],
                  inputs={key: md.digest(path.read_bytes()) for key, path in paths.items()},
                  manifest_sha256=md.digest(manifest_path.read_bytes()),
                  fixed_pv=value.fixed_pv, floating_pv=value.floating_pv, dirty_pv=value.pv,
                  accrued=value.accrued, clean_pv=value.clean_pv, par_rate=value.par_rate,
                  settled_coupons=value.settled_coupons, outstanding_coupons=len(value.coupons),
                  realized_observations=sum(o["realized"] for o in observations),
                  forecast_observations=sum(not o["realized"] for o in observations),
                  discount_calibration=calibration(calibrated),
                  projection_calibration=calibration(projection_calibration) if projection_calibration else None,
                  discount_zero_risk=buckets(risk.discount_zero), projection_zero_risk=buckets(risk.projection_zero),
                  risk_units="currency/bp; (PV_down - PV_up)/(2*bump_bps); realized fixings frozen",
                  cashflows=cashflows, observations=observations)
    if projection_snapshot is None:
        quote_risk = r.sofr_quote_risk(builder, market, swap, fixings, ts)
        report.update(parallel_quote_dv01=quote_risk.parallel_dv01, quote_risk=buckets(quote_risk.quote_buckets))
    else:
        quote_risk = r.sofr_multi_curve_quote_risk(builder, forward_builder, market, forward_quotes, swap, fixings, ts)
        report.update(discount_parallel_quote_dv01=quote_risk.discount_parallel_dv01,
                      projection_parallel_quote_dv01=quote_risk.projection_parallel_dv01,
                      discount_quote_risk=buckets(quote_risk.discount_quotes),
                      projection_quote_risk=buckets(quote_risk.projection_quotes),
                      quote_risk_method="Discount shocks rebuild both curves at fixed projection quotes; projection shocks hold discounting fixed")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--module-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = run(args.manifest, args.module_dir)
        args.out.mkdir(parents=True, exist_ok=True)
        # The complete report is content addressed and atomically published; CSVs
        # are disposable views, named by that same report digest.
        path = md.write_snapshot(args.out, report)
        for key in ("cashflows", "observations"):
            rows = report[key]
            if rows:
                with (args.out / f"{path.stem}-{key}.csv").open("w", newline="") as stream:
                    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                    writer.writeheader()
                    writer.writerows(rows)
        print(json.dumps({k: report[k] for k in ("as_of", "synthetic_quotes", "dirty_pv", "accrued", "clean_pv",
                                               "realized_observations", "forecast_observations")}, indent=2))
        print(path)
    except (ValueError, RuntimeError, KeyError, TypeError, OSError) as error:
        parser.exit(1, f"Valuation rejected: {error}\n")


if __name__ == "__main__":
    main()
