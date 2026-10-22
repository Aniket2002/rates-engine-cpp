"""Hand-solvable hedge optimizer cases, including binding limits and singularity."""
import unittest
from hedge_study import bounded_hedge


class HedgeSolverTest(unittest.TestCase):
    def test_exact_hedge(self):
        x, residual = bounded_hedge([2., -3.], [[1., 0.], [0., 1.]], 4.)
        self.assertEqual(x, [-2., 3.])
        self.assertEqual(residual, [0., 0.])

    def test_active_bound(self):
        x, residual = bounded_hedge([2., -3.], [[1., 0.], [0., 1.]], 1.)
        self.assertEqual(x, [-1., 1.])
        self.assertEqual(residual, [1., -2.])

    def test_correlated_hedges(self):
        x, residual = bounded_hedge([1., 2., 1.], [[1., 1., 0.], [0., 1., 1.]], 2.)
        for actual in x:
            self.assertAlmostEqual(actual, -1.)
        self.assertLess(sum(v*v for v in residual), 1e-25)

    def test_bad_universe(self):
        for target, columns, bound in [([1.], [[1.], [1.]], 1.), ([1.], [[1.]], 0.),
                                       ([float('nan')], [[1.]], 1.), ([1.], [[1., 2.]], 1.)]:
            with self.assertRaises(ValueError):
                bounded_hedge(target, columns, bound)


if __name__ == "__main__":
    unittest.main()
