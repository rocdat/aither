#   This file is part of aither.
#   Copyright (C) 2015-17  Michael Nucci (michael.nucci@gmail.com)
#
#   Aither is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   Aither is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>. 
#
#   This script runs regression tests to test builds on linux and macOS for
#   travis ci, and windows for appveyor

import os
import optparse
import shutil
import sys
import datetime
import subprocess
import time

class regressionTest:
    def __init__(self):
        self.caseName = "none"
        self.iterations = 100
        self.procs = 1
        self.residuals = [1.0, 1.0, 1.0, 1.0, 1.0]
        self.ignoreIndices = []
        self.location = os.getcwd()
        self.runDirectory = "."
        self.aitherPath = "aither"
        self.mpirunPath = "mpirun"
        self.percentTolerance = 0.01
        self.isRestart = False
        self.restartFile = "none"

    def SetRegressionCase(self, name):
        self.caseName = name

    def SetNumberOfIterations(self, num):
        self.iterations = num

    def SetNumberOfProcessors(self, num):
        self.procs = num

    def Processors(self):
        return self.procs

    def SetResiduals(self, resid):
        self.residuals = resid

    def SetRunDirectory(self, path):
        self.runDirectory = path

    def SetAitherPath(self, path):
        self.aitherPath = path

    def SetMpirunPath(self, path):
        self.mpirunPath = path

    def SetIgnoreIndices(self, ind):
        self.ignoreIndices.append(ind)

    def SetPercentTolerance(self, per):
        self.percentTolerance = per

    def GoToRunDirectory(self):
        os.chdir(self.runDirectory)

    def SetRestart(self, resFlag):
        self.isRestart = resFlag

    def SetRestartFile(self, resFile):
        self.restartFile = resFile

    def ReturnToHomeDirectory(self):
        os.chdir(self.location)

    def GetTestCaseResiduals(self):
        fname = self.caseName + ".resid"
        rfile = open(fname, "r")
        lastLine = rfile.readlines()[-1]
        rfile.close()
        tokens = lastLine.split()
        resids = [float(ii) for ii in tokens[3:3+len(self.residuals)]]
        return resids

    def CompareResiduals(self, returnCode):
        testResids = self.GetTestCaseResiduals()
        resids = []
        truthResids = []
        for ii in range(0, len(testResids)):
            if ii not in self.ignoreIndices:
                resids.append(testResids[ii])
                truthResids.append(self.residuals[ii])
        if (returnCode == 0):
            passing = [abs(resid - truthResids[ii]) <= self.percentTolerance * truthResids[ii]
                       for ii, resid in enumerate(resids)]
        else:
            passing = [False for ii in resids]
        return passing, resids

    def GetResiduals(self):
        return self.residuals
        
    # change input file to have number of iterations specified for test
    def ModifyInputFile(self):
        fname = self.caseName + ".inp"
        fnameBackup = fname + ".old"
        shutil.move(fname, fnameBackup)
        with open(fname, "w") as fout:
            with open(fnameBackup, "r") as fin:
                for line in fin:
                    if "iterations:" in line:
                        fout.write("iterations: " + str(self.iterations) + "\n")
                    elif "outputFrequency:" in line:
                        fout.write("outputFrequency: " + str(self.iterations) + "\n")
                    else:
                        fout.write(line)

    # modify the input file and run the test
    def RunCase(self):
        self.GoToRunDirectory()
        print("---------- Starting Test:", self.caseName, "----------")
        print("Current directory:", os.getcwd())
        print("Modifying input file...")
        self.ModifyInputFile()
        if self.isRestart:
            cmd = self.mpirunPath + " -np " + str(self.procs) + " " + self.aitherPath \
                  + " " + self.caseName + ".inp " + self.restartFile + " > " + self.caseName \
                  + ".out"
        else:
            cmd = self.mpirunPath + " -np " + str(self.procs) + " " + self.aitherPath \
                  + " " + self.caseName + ".inp > " + self.caseName + ".out"
        print(cmd)
        start = datetime.datetime.now()
        interval = start
        process = subprocess.Popen(cmd, shell=True)
        while process.poll() is None:
            current = datetime.datetime.now()
            if (current - interval).total_seconds() > 60.:
                print("----- Run Time: %s -----" % (current - start))
                interval = current
            time.sleep(0.5)
        returnCode = process.poll()

        if (returnCode == 0):
            print("Simulation completed with no errors")
        else:
            print("ERROR: Simulation terminated with errors")
        duration = datetime.datetime.now() - start

        # test residuals for pass/fail
        passed, resids = self.CompareResiduals(returnCode)
        if all(passed):
            print("All tests for", self.caseName, "PASSED!")
        else:
            print("Tests for", self.caseName, "FAILED!")
            print("Residuals should be:", self.GetResiduals())
            print("Residuals are:", resids)

        print("Test Duration:", duration)
        print("---------- End Test:", self.caseName, "----------")
        print("")
        print("")
        self.ReturnToHomeDirectory()
        return passed


def main():
    # Set up options
    parser = optparse.OptionParser()
    parser.add_option("-a", "--aitherPath", action="store", dest="aitherPath",
                      default="aither", 
                      help="Path to aither executable. Default = aither")
    parser.add_option("-o", "--operatingSystem", action="store",
                      dest="operatingSystem", default="linux",
                      help="Operating system that tests will run on [linux/macOS/windows]. Default = linux")
    parser.add_option("-m", "--mpirunPath", action="store",
                      dest="mpirunPath", default="mpirun",
                      help="Path to mpirun. Default = mpirun")
                      
    options, remainder = parser.parse_args()

    # travis macOS images have 1 proc, ubuntu have 2
    # appveyor windows images have 2 procs
    if (options.operatingSystem == "linux" or options.operatingSystem == "windows"):
        maxProcs = 2
    else:
        maxProcs = 1

    numIterations = 100
    numIterationsShort = 20
    numIterationsRestart = 50
    totalPass = True

    # ------------------------------------------------------------------
    # Regression tests
    # ------------------------------------------------------------------

    # ------------------------------------------------------------------
    # subsonic cylinder
    # laminar, inviscid, lu-sgs
    subCyl = regressionTest()
    subCyl.SetRegressionCase("subsonicCylinder")
    subCyl.SetAitherPath(options.aitherPath)
    subCyl.SetRunDirectory("subsonicCylinder")
    subCyl.SetNumberOfProcessors(1)
    subCyl.SetNumberOfIterations(numIterations)
    subCyl.SetResiduals([1.5371e-1, 1.4991e-1, 1.5910e-1, 8.2250e-1, 1.5297e-1])
    subCyl.SetIgnoreIndices(3)
    subCyl.SetMpirunPath(options.mpirunPath)

    # run regression case
    passed = subCyl.RunCase()
    totalPass = totalPass and all(passed)

    # ------------------------------------------------------------------
    # multi-block subsonic cylinder
    # laminar, inviscid, lusgs, multi-block, ausmpw+
    multiCyl = regressionTest()
    multiCyl.SetRegressionCase("multiblockCylinder")
    multiCyl.SetAitherPath(options.aitherPath)
    multiCyl.SetRunDirectory("multiblockCylinder")
    multiCyl.SetNumberOfProcessors(maxProcs)
    multiCyl.SetNumberOfIterations(numIterations)
    multiCyl.SetResiduals([2.3117e-01, 2.5907e-01, 4.0735e-01, 1.0640e+00,
                           2.2955e-01])
    multiCyl.SetIgnoreIndices(3)
    multiCyl.SetMpirunPath(options.mpirunPath)

    # run regression case
    passed = multiCyl.RunCase()
    totalPass = totalPass and all(passed)

    # ------------------------------------------------------------------
    # sod shock tube
    # laminar, inviscid, bdf2, weno
    shockTube = regressionTest()
    shockTube.SetRegressionCase("shockTube")
    shockTube.SetAitherPath(options.aitherPath)
    shockTube.SetRunDirectory("shockTube")
    shockTube.SetNumberOfProcessors(1)
    shockTube.SetNumberOfIterations(numIterations)
    shockTube.SetResiduals([5.0503e-1, 4.4569e-1, 1.0e0, 1.0e0, 2.6181e-1])
    shockTube.SetIgnoreIndices(2)
    shockTube.SetIgnoreIndices(3)
    shockTube.SetMpirunPath(options.mpirunPath)

    # run regression case
    passed = shockTube.RunCase()
    totalPass = totalPass and all(passed)

    # ------------------------------------------------------------------
    # sod shock tube restart
    # laminar, inviscid, bdf2, weno
    shockTubeRestart = shockTube
    shockTubeRestart.SetNumberOfIterations(numIterationsRestart)
    shockTubeRestart.SetRestart(True)
    shockTubeRestart.SetRestartFile("shockTube_50.rst")

    # run regression case
    passed = shockTubeRestart.RunCase()
    totalPass = totalPass and all(passed)

    # ------------------------------------------------------------------
    # supersonic wedge
    # laminar, inviscid, explicit euler
    supWedge = regressionTest()
    supWedge.SetRegressionCase("supersonicWedge")
    supWedge.SetAitherPath(options.aitherPath)
    supWedge.SetRunDirectory("supersonicWedge")
    supWedge.SetNumberOfProcessors(1)
    supWedge.SetNumberOfIterations(numIterations)
    supWedge.SetResiduals([4.1813e-1, 4.2549e-1, 3.6525e-1, 3.9971e-1, 4.0998e-1])
    supWedge.SetIgnoreIndices(3)
    supWedge.SetMpirunPath(options.mpirunPath)

    # run regression case
    passed = supWedge.RunCase()
    totalPass = totalPass and all(passed)

    # ------------------------------------------------------------------
    # transonic bump in channel
    # laminar, inviscid, dplur
    transBump = regressionTest()
    transBump.SetRegressionCase("transonicBump")
    transBump.SetAitherPath(options.aitherPath)
    transBump.SetRunDirectory("transonicBump")
    transBump.SetNumberOfProcessors(1)
    transBump.SetNumberOfIterations(numIterations)
    transBump.SetResiduals([1.1839e-1, 6.8615e-2, 8.4925e-2, 1.0000, 9.9669e-2])
    transBump.SetIgnoreIndices(3)
    transBump.SetMpirunPath(options.mpirunPath)

    # run regression case
    passed = transBump.RunCase()
    totalPass = totalPass and all(passed)

    # ------------------------------------------------------------------
    # viscous flat plate
    # laminar, viscous, lu-sgs
    viscPlate = regressionTest()
    viscPlate.SetRegressionCase("viscousFlatPlate")
    viscPlate.SetAitherPath(options.aitherPath)
    viscPlate.SetRunDirectory("viscousFlatPlate")
    viscPlate.SetNumberOfProcessors(maxProcs)
    viscPlate.SetNumberOfIterations(numIterations)
    if viscPlate.Processors() == 2:
        viscPlate.SetResiduals([7.7239e-2, 2.4713e-1, 5.6557e-2, 8.4112e-1, 7.9342e-2])
    else:
        viscPlate.SetResiduals([7.6467e-2, 2.4714e-1, 4.0109e-2, 8.3161e-1, 7.9240e-2])
    viscPlate.SetIgnoreIndices(3)
    viscPlate.SetMpirunPath(options.mpirunPath)

    # run regression case
    passed = viscPlate.RunCase()
    totalPass = totalPass and all(passed)

    # ------------------------------------------------------------------
    # turbulent flat plate
    # viscous, lu-sgs, k-w wilcox
    turbPlate = regressionTest()
    turbPlate.SetRegressionCase("turbFlatPlate")
    turbPlate.SetAitherPath(options.aitherPath)
    turbPlate.SetRunDirectory("turbFlatPlate")
    turbPlate.SetNumberOfProcessors(maxProcs)
    turbPlate.SetNumberOfIterations(numIterationsShort)
    if turbPlate.Processors() == 2:
        turbPlate.SetResiduals([2.2326e-01, 2.9704e-01, 4.5442e-01, 2.4928e-01,
                                2.1792e-01, 7.9769e-07, 2.3288e-04])
    else:
        turbPlate.SetResiduals([2.1828e-01, 2.9702e-01, 4.5628e-01, 2.4928e-01,
                                2.1361e-01, 7.9753e-07, 2.3287e-04])
    turbPlate.SetIgnoreIndices(2)
    turbPlate.SetMpirunPath(options.mpirunPath)

    # run regression case
    passed = turbPlate.RunCase()
    totalPass = totalPass and all(passed)

    # ------------------------------------------------------------------
    # rae2822
    # turbulent, k-w sst, c-grid
    rae2822 = regressionTest()
    rae2822.SetRegressionCase("rae2822")
    rae2822.SetAitherPath(options.aitherPath)
    rae2822.SetRunDirectory("rae2822")
    rae2822.SetNumberOfProcessors(maxProcs)
    rae2822.SetNumberOfIterations(numIterationsShort)
    if rae2822.Processors() == 2:
        rae2822.SetResiduals([5.5472e-01, 7.2623e-01, 5.0035e-01, 4.8794e-01,
                              4.9827e-01, 2.4542e-05, 9.3450e-05])
    else:
        rae2822.SetResiduals([5.5195e-01, 7.2220e-01, 5.0410e-01, 6.9139e-01,
                              4.9487e-01, 2.4542e-05, 9.2871e-05])
    rae2822.SetIgnoreIndices(3)
    rae2822.SetMpirunPath(options.mpirunPath)

    # run regression case
    passed = rae2822.RunCase()
    totalPass = totalPass and all(passed)

    # ------------------------------------------------------------------
    # couette flow
    # laminar, viscous, periodic bcs, moving wall, isothermal wall
    couette = regressionTest()
    couette.SetRegressionCase("couette")
    couette.SetAitherPath(options.aitherPath)
    couette.SetRunDirectory("couette")
    couette.SetNumberOfProcessors(1)
    couette.SetNumberOfIterations(numIterations)
    couette.SetResiduals([1.1343e-1, 5.0725e-1, 7.4086e-2, 4.7218e-1, 2.2789e-1])
    couette.SetIgnoreIndices(3)
    couette.SetMpirunPath(options.mpirunPath)

    # run regression case
    passed = couette.RunCase()
    totalPass = totalPass and all(passed)

    # ------------------------------------------------------------------
    # wall law
    # wall law bc, turbulent, blusgs
    wallLaw = regressionTest()
    wallLaw.SetRegressionCase("wallLaw")
    wallLaw.SetAitherPath(options.aitherPath)
    wallLaw.SetRunDirectory("wallLaw")
    wallLaw.SetNumberOfProcessors(maxProcs)
    wallLaw.SetNumberOfIterations(numIterationsShort)
    if wallLaw.Processors() == 2:
        wallLaw.SetResiduals([8.1949e-01, 1.0542e-01, 1.3522e-01, 9.2939e-01,
                              8.5213e-01, 6.0529e-02, 6.7596e-05])
    else:
        wallLaw.SetResiduals([8.1310e-01, 1.0392e-01, 1.3302e-01, 9.2927e-01,
                              8.4532e-01, 6.0527e-02, 6.7585e-05])
    wallLaw.SetIgnoreIndices(1)
    wallLaw.SetMpirunPath(options.mpirunPath)

    # run regression case
    passed = wallLaw.RunCase()
    totalPass = totalPass and all(passed)

    # ------------------------------------------------------------------
    # thermally perfect gas
    # turbulent, thermally perfect, supersonic
    thermallyPerfect = regressionTest()
    thermallyPerfect.SetRegressionCase("thermallyPerfect")
    thermallyPerfect.SetAitherPath(options.aitherPath)
    thermallyPerfect.SetRunDirectory("thermallyPerfect")
    thermallyPerfect.SetNumberOfProcessors(maxProcs)
    thermallyPerfect.SetNumberOfIterations(numIterationsShort)
    if thermallyPerfect.Processors() == 2:
        thermallyPerfect.SetResiduals([5.8862e-01, 3.8007e-01, 4.9681e-01,
                                       8.4268e-03, 6.0802e-01, 3.5653e-02,
                                       1.4414e-02])
    else:
        thermallyPerfect.SetResiduals([5.8862e-01, 3.8007e-01, 4.9681e-01,
                                       1.9063e-03, 6.0803e-01, 3.5651e-02,
                                       1.4414e-02])
    thermallyPerfect.SetIgnoreIndices(3)
    thermallyPerfect.SetMpirunPath(options.mpirunPath)

    # run regression case
    passed = thermallyPerfect.RunCase()
    totalPass = totalPass and all(passed)

    # ------------------------------------------------------------------
    # uniform flow
    # turbulent, all 8 block-to-block orientations
    uniform = regressionTest()
    uniform.SetRegressionCase("uniformFlow")
    uniform.SetAitherPath(options.aitherPath)
    uniform.SetRunDirectory("uniformFlow")
    uniform.SetNumberOfProcessors(1)
    uniform.SetNumberOfIterations(numIterationsShort)
    uniform.SetResiduals([2.6167e-01, 3.2443e-01, 1.8594e-01, 1.8633e-01,
                          2.5828e-01, 7.7757e-09, 2.4621e-09])
    uniform.SetMpirunPath(options.mpirunPath)

    # run regression case
    passed = uniform.RunCase()
    totalPass = totalPass and all(passed)

    # ------------------------------------------------------------------
    # regression test overall pass/fail
    # ------------------------------------------------------------------
    if totalPass:
        print("All tests passed!")
        sys.exit(0)
    else:
        print("ERROR: Some tests failed")
        sys.exit(1)


if __name__ == "__main__":
    main()
