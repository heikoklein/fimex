import pyfimex0
import numpy
import os.path
import unittest

test_srcdir = os.path.join(os.path.dirname(__file__), "..", "..", "test")

class TestReader(unittest.TestCase):

    def test_OpenAndInspect(self):
        test_ncfile = os.path.join(test_srcdir, 'testdata_vertical_ensemble_in.nc')
        r = pyfimex0.createFileReader('netcdf', test_ncfile)

        self.assertTrue(type(r.getDataSlice('hybrid',0).values()[0]) is numpy.float64)

        r_cdm = r.getCDM()

        v_xwind10m = r_cdm.getVariable('x_wind_10m')
        v_xwind10m_name = v_xwind10m.getName()
        self.assertEqual('x_wind_10m', v_xwind10m_name)

        d_ens = r_cdm.getDimension('ensemble_member')
        d_ens_len = d_ens.getLength()
        self.assertEqual(3, d_ens_len)

        r_dims = r_cdm.getDimensionNames()
        self.assertTrue('time' in r_dims)

        r_vars = r_cdm.getVariableNames()
        self.assertTrue('surface_geopotential' in r_vars)


if __name__ == '__main__':
    unittest.main()