"""Analytic and adversarial tests for the C++ cash-flow LP and optimizer adapter."""
import argparse
from datetime import date
import json
import math
from pathlib import Path
import sys
import unittest
from unittest import mock

import numpy as np

from curve_ambiguity import check_witness, run_study, solve_range

parser = argparse.ArgumentParser()
parser.add_argument("--module-dir", type=Path, required=True)
args, remaining = parser.parse_known_args()
sys.path.insert(0, str(args.module_dir.resolve()))
import pyrates as r


class AmbiguityTests(unittest.TestCase):
    def setUp(self):
        self.v = r.Date("2025-01-02")
        self.c = r.Conventions()
        self.c.business_day = r.BusinessDay.Unadjusted

    def band(self, rate=.03, label="1Y", months=12, kind=None, spread=0):
        q = r.MarketQuote(kind or r.Instrument.ContinuousZero, label, self.v.add_months(months), rate)
        return r.QuoteBand(q, rate - spread, rate + spread)

    def trade(self, months=6, coupon=0, side=None, effective=None):
        return r.Swap(effective or self.v, self.v.add_months(months), 1e6, coupon,
                      side or r.Side.Payer, self.c)

    def problem(self, bands=None, trades=None, lower=-.01, upper=.10, projection=None):
        return r.curve_ambiguity_problem(self.v, bands or [self.band()], trades or [self.trade()],
                                          r.ForwardBounds(lower, upper), self.c, projection)

    def test_closed_form_interior_discount_bounds(self):
        # The endpoint DF is fixed; the two adjacent forward constraints give
        # an independent closed-form interval for an unquoted half-year DF.
        for rate, lower, upper in ((.03, -.01, .10), (-.02, -.05, 0.0), (0.0, 0.0, 0.0)):
            p = self.problem([self.band(rate)], lower=lower, upper=upper)
            result = solve_range(p)
            tau = (date(2025, 7, 2) - date(2025, 1, 2)).days / 365
            end = math.exp(-rate)
            lo = max(math.exp(-upper * tau), end * math.exp(lower * (1 - tau)))
            hi = min(math.exp(-lower * tau), end * math.exp(upper * (1 - tau)))
            self.assertAlmostEqual(result["minimum"]["pv"], 1e6 * (1 - hi), delta=2e-6)
            self.assertAlmostEqual(result["maximum"]["pv"], 1e6 * (1 - lo), delta=2e-6)

    def test_quoted_swap_is_identified_but_intermediate_trade_is_not(self):
        q = self.band(.04, months=24, kind=r.Instrument.Swap)
        identified = solve_range(self.problem([q], [self.trade(24, .04)]))
        self.assertLess(identified["width"], 2e-6)
        self.assertAlmostEqual(identified["minimum"]["pv"], 0, delta=2e-6)
        ambiguous = solve_range(self.problem([q], [self.trade(18, .04)]))
        self.assertGreater(ambiguous["width"], 1000)

    def test_more_information_narrows_and_wider_spreads_expand(self):
        original = solve_range(self.problem())
        wider = solve_range(self.problem([self.band(spread=.001)]))
        more = solve_range(self.problem([self.band(), self.band(.025, label="6M", months=6)]))
        self.assertLessEqual(wider["minimum"]["pv"], original["minimum"]["pv"] + 1e-6)
        self.assertGreaterEqual(wider["maximum"]["pv"], original["maximum"]["pv"] - 1e-6)
        self.assertLess(more["width"], 2e-6)

    def test_inconsistent_quotes_and_forward_bounds_fail_closed(self):
        with self.assertRaisesRegex(ValueError, "status 2"):
            solve_range(self.problem([self.band(.03), self.band(.04, label="conflict")]))
        with self.assertRaisesRegex(ValueError, "status 2"):
            solve_range(self.problem(lower=.05, upper=.10))

    def test_payer_receiver_symmetry_and_portfolio_netting(self):
        payer = solve_range(self.problem(trades=[self.trade(coupon=.03)]))
        receiver = solve_range(self.problem(trades=[self.trade(coupon=.03, side=r.Side.Receiver)]))
        self.assertAlmostEqual(payer["minimum"]["pv"], -receiver["maximum"]["pv"], delta=2e-6)
        net = solve_range(self.problem(trades=[self.trade(), self.trade(side=r.Side.Receiver)]))
        self.assertEqual(net["width"], 0)

    def test_dual_witness_reconstructs_bound(self):
        p = self.problem()
        result = solve_range(p)
        a = np.array([row.coefficients for row in p.constraints])
        b = np.array([row.upper for row in p.constraints])
        c = np.array(p.objective)
        for name in ("minimum", "maximum"):
            witness = result[name]
            x = np.array(witness["discounts"])
            y = np.array(witness["dual_inequality"])
            anchor = np.zeros(len(c))
            anchor[0] = witness["dual_anchor"]
            objective = c * witness["objective_direction"] / witness["objective_scale"]
            self.assertLess(np.max(np.abs(a.T @ y + anchor - objective)), 1e-9)
            self.assertLessEqual(np.max(y), 1e-10)
            self.assertLess(abs(objective @ x - b @ y - witness["dual_anchor"]), 1e-9)
            self.assertAlmostEqual(r.price(p.witness(x.tolist()), self.trade()).pv, witness["pv"], delta=2e-6)

    def test_fixed_projection_and_forward_starting_target(self):
        projection = r.DiscountCurve(self.v, [(self.v.add_months(12), math.exp(-.05))])
        trade = self.trade(months=9, coupon=.02, effective=self.v.add_months(3))
        p = self.problem(trades=[trade], projection=projection)
        result = solve_range(p)
        for name in ("minimum", "maximum"):
            curve = p.witness(result[name]["discounts"])
            pv = r.price_multicurve(curve, projection, trade).pv
            self.assertAlmostEqual(pv, result[name]["pv"], delta=2e-6)

    def study_input(self):
        return {"schema_version": 1, "valuation": "2025-01-02", "forward_bounds": [-.01, .10],
                "quotes": [{"type": "zero", "label": "1Y", "maturity": "2026-01-02",
                            "bid": .03, "ask": .03}],
                "portfolio": [{"effective": "2025-01-02", "maturity": "2025-07-02",
                               "notional": 1e6, "coupon": 0, "side": "payer"}],
                "forward_bound_sweep": [[.01, .06]]}

    def test_serialized_sweeps_retain_repriceable_curves_and_dual_certificates(self):
        report = json.loads(json.dumps(run_study(r, self.study_input(), require_quantlib=True)))
        sweeps = report["spread_sweep"] + report["forward_bound_sweep"]
        self.assertEqual(len(sweeps), 5)
        self.assertEqual(report["validation"]["repriced_witnesses"], 12)
        self.assertEqual(report["validation"]["quantlib_comparisons"], 24)
        for sweep in sweeps:
            a = np.array([row["coefficients"] for row in sweep["lp"]["constraints"]])
            b = np.array([row["upper"] for row in sweep["lp"]["constraints"]])
            c = np.array(sweep["lp"]["objective"])
            for name in ("minimum", "maximum"):
                witness = sweep["bounds"][name]
                x = np.array(witness["discounts"])
                y = np.array(witness["dual_inequality"])
                anchor = np.zeros(len(c))
                anchor[0] = witness["dual_anchor"]
                objective = c * witness["objective_direction"] / witness["objective_scale"]
                self.assertLessEqual(np.max(a @ x - b), 1e-9)
                self.assertAlmostEqual(x[0], 1, delta=1e-9)
                self.assertLess(np.max(np.abs(a.T @ y + anchor - objective)), 1e-9)
                self.assertLessEqual(np.max(y), 1e-10)
                self.assertLess(abs(objective @ x - b @ y - anchor[0]), 1e-9)
                self.assertEqual(witness["pv"], sweep[name + "_pv"])
                curve = r.DiscountCurve(self.v, [(r.Date(d), df)
                                                for d, df in zip(sweep["dates"][1:], x[1:])])
                self.assertAlmostEqual(r.price(curve, self.trade()).pv, witness["pv"], delta=2e-6)
                self.assertAlmostEqual(witness["quantlib_pv"], witness["pv"], delta=2e-6)
                self.assertEqual(witness["quantlib_comparisons"], 2)
                quote = witness["quote_repricing"][0]
                half_spread = sweep.get("added_half_spread_bps", 0) * 1e-4
                self.assertAlmostEqual(quote["bid"], .03 - half_spread)
                self.assertAlmostEqual(quote["ask"], .03 + half_spread)
                self.assertGreaterEqual(quote["model"], quote["bid"] - 1e-9)
                self.assertLessEqual(quote["model"], quote["ask"] + 1e-9)

    def test_sweep_repricing_failure_aborts_study(self):
        data = self.study_input()
        for sweep in ("spread", "forward"):
            def reject_sweep(module, inputs, problem, witness, require_quantlib):
                changed = (inputs["quotes"] != data["quotes"] if sweep == "spread"
                           else inputs["forward_bounds"] != data["forward_bounds"])
                if changed:
                    raise AssertionError("Rejected sweep witness")
                return check_witness(module, inputs, problem, witness, require_quantlib)
            with self.subTest(sweep=sweep), mock.patch("curve_ambiguity.check_witness", side_effect=reject_sweep):
                with self.assertRaisesRegex(AssertionError, "Rejected sweep witness"):
                    run_study(r, data, require_quantlib=True)

    def test_deposit_band_reprices_at_both_extrema(self):
        p = self.problem([self.band(.03, kind=r.Instrument.Deposit, spread=.002)], [self.trade(12)])
        result = solve_range(p)
        tau = 365 / 360
        for name, rate in (("minimum", .028), ("maximum", .032)):
            self.assertAlmostEqual(result[name]["pv"], 1e6 * (1 - 1 / (1 + rate * tau)), delta=2e-6)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]] + remaining)
