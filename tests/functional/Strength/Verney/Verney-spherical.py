#-------------------------------------------------------------------------------
# Spherical (1D R) version.
# An idealized strength test test where an imploding shell is ultimately stopped
# at a known radius due to plastic work dissipation.
#
# See LA-14379, Howell & Ball 2002 JCP
#-------------------------------------------------------------------------------
#
# Ordinary solid SPH
#
#ATS:t100 = test(        SELF, "--graphics None --clearDirectories True  --checkError True   --restartStep 20", label="Spherical Verney problem with solid SPH -- 1-D (serial)")
#ATS:t101 = testif(t100, SELF, "--graphics None --clearDirectories False --checkError False  --restartStep 20 --restoreCycle 20 --steps 20 --checkRestart True", label="Spherical Verney problem with solid SPH -- 1-D (serial) RESTART CHECK")
#ATS:t102 = test(        SELF, "--graphics None --clearDirectories True  --checkError True  --dataDirBase 'dumps-spherical-restartcheck' --restartStep 20", np=2, label="Spherical Verney problem with solid SPH -- 1-D (parallel)")
#ATS:t103 = testif(t102, SELF, "--graphics None --clearDirectories False --checkError False --dataDirBase 'dumps-spherical-restartcheck' --restartStep 20 --restoreCycle 20 --steps 20 --checkRestart True", np=2, label="Spherical Verney problem with solid SPH -- 1-D (parallel) RESTART CHECK")
#ATS:t104 = test(        SELF, "--graphics None --clearDirectories True  --checkError True  --dataDirBase 'dumps-spherical-reproducing' --domainIndependent True --outputFile 'Verney-spherical-1proc-reproducing.txt'", label="Spherical Verney problem with solid SPH -- 1-D (serial reproducing test setup)")
#ATS:t105 = testif(t104, SELF, "--graphics None --clearDirectories False  --checkError True  --dataDirBase 'dumps-spherical-reproducing' --domainIndependent True --outputFile 'Verney-spherical-4proc-reproducing.txt' --comparisonFile 'Verney-spherical-1proc-reproducing.txt'", np=4, label="Spherical Verney problem with solid SPH -- 1-D (4 proc reproducing test)")

from math import *
import shutil
import mpi

from SphericalSpheral import *
from SpheralTestUtilities import *
from findLastRestart import *
from NodeHistory import NodeHistory
from GenerateSphericalNodeProfile1d import *
from SortAndDivideRedistributeNodes import distributeNodes1d

#-------------------------------------------------------------------------------
# Identify ourselves!
#-------------------------------------------------------------------------------
title("Spherical Verney imploding shell test.")

#-------------------------------------------------------------------------------
# Use (cm, gm, usec) units as in paper.
#-------------------------------------------------------------------------------
units = PhysicalConstants(0.01,  # Unit length in m
                          0.001, # Unit mass in kg
                          1e-6)  # Unit length in sec

#-------------------------------------------------------------------------------
# The analytic function paramterizing the shell evolution.
#-------------------------------------------------------------------------------
def F(alpha, lamb, R0, R1, n):

    class integfunc(ScalarScalarFunctor):
        def __init__(self, R0, R1):
            ScalarScalarFunctor.__init__(self)
            self.alpha = R1/R0 - 1.0
            self.thpt = 3.0*(self.alpha +
                             self.alpha*self.alpha +
                             self.alpha*self.alpha*self.alpha)
            return
        def __call__(self, x):
            return x*x*log(1.0 + self.thpt/(x*x*x))

    func = integfunc(R0, R1)
    return simpsonsIntegrationDouble(func, lamb, 1.0, n)

#-------------------------------------------------------------------------------
# Generic problem parameters
# All (cm, gm, usec) units.
#-------------------------------------------------------------------------------
commandLine(nr = 20,                     # Radial resolution of the shell in points

            nPerh = 4.01,

            # Material specific bounds on the mass density.
            etamin = 1e-3,
            etamax = 1e3,

            # How many shells should we create tracer histories for?
            nshells = 10,

            # Hydro parameters.
            crksph = False,
            sph = False,   # This just chooses the H algorithm -- you can use this with CRKSPH for instance.
            Cl = None,
            Cq = None,
            Qself = None,
            linearInExpansion = None,
            Qlimiter = None,
            balsaraCorrection = None,
            epsilon2 = None,
            negligibleSoundSpeed = None,
            csMultiplier = None,
            hmin = 1e-5,
            hmax = 10.0,
            cfl = 0.25,
            useVelocityMagnitudeForDt = False,
            XSPH = True,
            epsilonTensile = 0.0,
            nTensile = 4,
            filter = 0.0,
            HUpdate = IdealH,
            densityUpdate = IntegrateDensity,
            compatibleEnergy = True,
            evolveTotalEnergy = False,
            gradhCorrection = False,

            # Time integration
            IntegratorConstructor = VerletIntegrator,
            goalTime = 150.0,
            steps = None,
            dt = 1e-6,
            dtMin = 1e-6,
            dtMax = 10.0,
            dtGrowth = 2.0,
            maxSteps = None,
            statsStep = 10,
            domainIndependent = False,
            dtverbose = False,
            restoreCycle = -1,
            restartStep = 500,
            sampleFreq = 10,
            vizTime = 1.0,
            vizStep = 100,

            graphics = True,

            clearDirectories = False,
            dataDirBase = "dumps-Verney-Be-R",
            outputFile = "Verney-Be-R.gnu",
            comparisonFile = "None",

            # Testing
            checkRestart = False,
            checkError = False,
            rInnerCheck = 4.0,
            rInnerError = 0.1,
        )

# Material parameters for this test problem.
rho0Be = 1.845
R0, R1 = 8.0, 10.0        # Inner, outer initial radius
G0, Y0 = 1.510, 0.0033    # Shear modulus and yield strength
r0 = 4.0                  # Expected final inner radius
delta = R1 - R0
lamb = r0/R0
alpha = delta/R0
Fval = F(alpha, lamb, R0, R1, 1000)
u0 = sqrt(4.0*Y0*R1*Fval/(3.0*rho0Be*delta))
print "  lambda = %s\n  alpha = %s\n  F = %s\n  u0 = %s\n" % (lamb, alpha, Fval, u0)

if crksph:
    hydroname = "CRKSPH"
else:
    hydroname = "SPH"

# Directories.
dataDir = os.path.join(dataDirBase,
                       hydroname,
                       "densityUpdate=%s" % densityUpdate,
                       "compatibleEnergy=%s_totalEnergy=%s" % (compatibleEnergy, evolveTotalEnergy),
                       "nr=%i" % nr)
restartDir = os.path.join(dataDir, "restarts")
vizDir = os.path.join(dataDir, "visit")
vizBaseName = "Verney-Be-%i" % nr
restartBaseName = os.path.join(restartDir, "Verney-%i" % nr)
historyOutputName = os.path.join(dataDir, "Verney-shell%i.dat")

#-------------------------------------------------------------------------------
# Check if the necessary output directories exist.  If not, create them.
#-------------------------------------------------------------------------------
import os, sys
if mpi.rank == 0:
    if clearDirectories and os.path.exists(dataDir):
        shutil.rmtree(dataDir)
    if not os.path.exists(restartDir):
        os.makedirs(restartDir)
    if not os.path.exists(vizDir):
        os.makedirs(vizDir)
mpi.barrier()

#-------------------------------------------------------------------------------
# Material properties.
#-------------------------------------------------------------------------------
eosBe = OsborneEquationOfState( 1.85, etamin, etamax,     # Parameters from Howell & Ball 2002
                                0.951168,   # a1
                                0.345301,   # a2pos
                               -0.345301,   # asneg
                                0.926914,   # b0
                                2.948420,   # b1
                                0.507979,   # b2pos
                                0.507979,   # b2neg
                                0.564362,   # c0
                                0.620422,   # c1
                                0.0,        # c2pos
                                0.0,        # c2neg
                                0.8,
                                9.015,
                                units,
                                minimumPressure = -0.1)
strengthModelBe = ConstantStrength(G0, Y0)

#-------------------------------------------------------------------------------
# Create our interpolation kernels -- one for normal hydro interactions, and
# one for use with the artificial viscosity
#-------------------------------------------------------------------------------
W = WendlandC4Kernel()
output("W")

#-------------------------------------------------------------------------------
# Create the NodeLists.
#-------------------------------------------------------------------------------
nodesBe = makeSolidNodeList("Beryllium", eosBe, strengthModelBe,
                            nPerh = nPerh,
                            kernelExtent = W.kernelExtent,
                            hmin = hmin,
                            hmax = hmax,
                            rhoMin = etamin*rho0Be,
                            rhoMax = etamax*rho0Be,
                            xmin = -100.0*Vector.one,
                            xmax =  100.0*Vector.one)
nodeSet = [nodesBe]

#-------------------------------------------------------------------------------
# Set node properties (positions, masses, H's, etc.)
#-------------------------------------------------------------------------------
print "Generating node distribution."
gen = GenerateSphericalNodeProfile1d(nr = nr,
                                     rho = rho0Be,
                                     rmin = R0,
                                     rmax = R1,
                                     nNodePerh = nPerh)
distributeNodes1d((nodesBe, gen))
output("mpi.reduce(nodesBe.numInternalNodes, mpi.MIN)")
output("mpi.reduce(nodesBe.numInternalNodes, mpi.MAX)")
output("mpi.reduce(nodesBe.numInternalNodes, mpi.SUM)")

# Set node velocites.
pos = nodesBe.positions()
vel = nodesBe.velocity()
for i in xrange(nodesBe.numInternalNodes):
    ri = pos[i].magnitude()
    vel[i].x = -u0 * (R0/ri)**2

#-------------------------------------------------------------------------------
# Construct a DataBase to hold our node list
#-------------------------------------------------------------------------------
db = DataBase()
for n in nodeSet:
    db.appendNodeList(n)
del n
output("db")
output("db.numNodeLists")
output("db.numFluidNodeLists")

#-------------------------------------------------------------------------------
# Construct the hydro physics object.
#-------------------------------------------------------------------------------
if crksph:
    hydro = CRKSPH(dataBase = db,
                   W = W,
                   filter = filter,
                   cfl = cfl,
                   compatibleEnergyEvolution = compatibleEnergy,
                   evolveTotalEnergy = evolveTotalEnergy,
                   XSPH = XSPH,
                   densityUpdate = densityUpdate,
                   HUpdate = HUpdate)
else:
    hydro = SPH(dataBase = db,
                W = W,
                filter = filter,
                cfl = cfl,
                compatibleEnergyEvolution = compatibleEnergy,
                evolveTotalEnergy = evolveTotalEnergy,
                gradhCorrection = gradhCorrection,
                densityUpdate = densityUpdate,
                HUpdate = HUpdate,
                XSPH = XSPH,
                epsTensile = epsilonTensile,
                nTensile = nTensile)
output("hydro")
output("hydro.cfl")
output("hydro.useVelocityMagnitudeForDt")
output("hydro.HEvolution")
output("hydro.densityUpdate")
output("hydro.compatibleEnergyEvolution")
output("hydro.kernel")
output("hydro.PiKernel")

#-------------------------------------------------------------------------------
# Tweak the artificial viscosity.
#-------------------------------------------------------------------------------
q = hydro.Q
if not Cl is None:
    q.Cl = Cl
if not Cq is None:
    q.Cq = Cq
if not Qlimiter is None:
    q.limiter = Qlimiter
if not epsilon2 is None:
    q.epsilon2 = epsilon2
if not linearInExpansion is None:
    q.linearInExpansion = linearInExpansion
if not Qself is None:
    hydro.Qself = Qself
output("q")
output("q.Cl")
output("q.Cq")
output("q.limiter")
output("q.epsilon2")
output("q.linearInExpansion")
output("q.quadraticInExpansion")
output("hydro.Qself")

#-------------------------------------------------------------------------------
# Construct a time integrator.
#-------------------------------------------------------------------------------
integrator = IntegratorConstructor(db)
integrator.appendPhysicsPackage(hydro)
integrator.lastDt = dt
if dtMin:
    integrator.dtMin = dtMin
if dtMax:
    integrator.dtMax = dtMax
integrator.dtGrowth = dtGrowth
integrator.domainDecompositionIndependent = domainIndependent
integrator.verbose = dtverbose
output("integrator")
output("integrator.havePhysicsPackage(hydro)")
output("integrator.lastDt")
output("integrator.dtMin")
output("integrator.dtMax")
output("integrator.dtGrowth")
output("integrator.domainDecompositionIndependent")

#-------------------------------------------------------------------------------
# Select points to follow histories.
#-------------------------------------------------------------------------------
def verneySample(nodes, indices):
    mass = nodes.mass()
    pos = nodes.positions()
    vel = nodes.velocity()
    rho = nodes.massDensity()
    eps = nodes.specificThermalEnergy()
    P = ScalarField("pressure", nodes)
    nodes.pressure(P)
    ps = nodes.plasticStrain()
    H = nodes.Hfield()
    S = nodes.deviatoricStress()
    mshell = mpi.allreduce(sum([mass[i] for i in indices] + [0.0]), mpi.SUM)
    assert mshell > 0.0
    rshell = mpi.allreduce(sum([mass[i]*pos[i].magnitude() for i in indices] + [0.0]), mpi.SUM)
    vshell = mpi.allreduce(sum([mass[i]*vel[i].dot(pos[i].unitVector()) for i in indices] + [0.0]), mpi.SUM)
    rhoshell = mpi.allreduce(sum([mass[i]*rho[i] for i in indices] + [0.0]), mpi.SUM)
    epsshell = mpi.allreduce(sum([mass[i]*eps[i] for i in indices] + [0.0]), mpi.SUM)
    Pshell = mpi.allreduce(sum([mass[i]*P[i] for i in indices] + [0.0]), mpi.SUM)
    Sshell = mpi.allreduce(sum([mass[i]*S[i].Trace() for i in indices] + [0.0]), mpi.SUM)
    strainShell = mpi.allreduce(sum([mass[i]*ps[i] for i in indices] + [0.0]), mpi.SUM)
    hshell = mpi.allreduce(sum([mass[i]*2.0/(H[i].Trace()) for i in indices] + [0.0]), mpi.SUM)
    rshell /= mshell
    vshell /= mshell
    rhoshell /= mshell
    epsshell /= mshell
    Pshell /= mshell
    Sshell /= mshell
    strainShell /= mshell
    hshell /= mshell
    return rshell, vshell, rhoshell, epsshell, Pshell, Sshell, strainShell, hshell

# Find shells of points in binned radii
histories = []
dr = (R1 - R0)/nshells
pos = nodesBe.positions()
shellIndices = [[] for i in xrange(nr)]
for i in xrange(nodesBe.numInternalNodes):
    ishell = min(nr - 1, int((pos[i].magnitude() - R0)/dr + 0.5))
    shellIndices[ishell].append(i)
for ishell in xrange(nshells):
    n = mpi.allreduce(len(shellIndices[ishell]), mpi.SUM)
    print "Selected %i nodes for shell %i." % (n, ishell)
    if n > 0:
        histories.append(NodeHistory(nodesBe, shellIndices[ishell], verneySample, historyOutputName % ishell, 
                                     labels = ("r", "vel", "rho", "eps", "pressure", "Srr", "plastic strain", "h")))

#-------------------------------------------------------------------------------
# Build the controller.
#-------------------------------------------------------------------------------
control = SpheralController(integrator, 
                            statsStep = statsStep,
                            restartStep = restartStep,
                            restartBaseName = restartBaseName,
                            restoreCycle = restoreCycle,
                            vizBaseName = vizBaseName,
                            vizDir = vizDir,
                            vizTime = vizTime,
                            vizStep = vizStep,
                            periodicWork = [(hist.sample, sampleFreq) for hist in histories],
                            iterateInitialH = (HUpdate == IntegrateH))
output("control")

#-------------------------------------------------------------------------------
# Advance to the end time.
#-------------------------------------------------------------------------------
if not steps is None:
    if checkRestart:
        control.setRestartBaseName(restartBaseName + "_CHECK")
    control.step(steps)
    if checkRestart:
        control.setRestartBaseName(restartBaseName)

    # Are we doing the restart test?
    if checkRestart:
        state0 = State(db, integrator.physicsPackages())
        state0.copyState()
        print control.totalSteps
        control.loadRestartFile(control.totalSteps)
        state1 = State(db, integrator.physicsPackages())
        if not state1 == state0:
            raise ValueError, "The restarted state does not match!"
        else:
            print "Restart check PASSED."

else:
    control.advance(goalTime)
    control.dropRestartFile()
    control.conserve.writeHistory(os.path.join(dataDir, "conservation_history.gnu"))

    # Check the tracer info for the innermost-shell, reporting the discrepancy
    # from the expected final stopping radius.  Not quite correct since we're sampling
    # the interior of the shell, but it gives us something quantitative.
    histories[0].sample(control.totalSteps, control.time(), control.lastDt())
    rsim0 = histories[0].sampleHistory[-1][0]
    print "Simulation measured final inner shell radius of %g cm: errror %g cm." % (rsim0,
                                                                                    rsim0 - r0)
Eerror = (control.conserve.EHistory[-1] - control.conserve.EHistory[0])/control.conserve.EHistory[0]
print "Total energy error: %g" % Eerror

#-------------------------------------------------------------------------------
# If requested, write out the state in a global ordering to a file.
#-------------------------------------------------------------------------------
if outputFile != "None":
    from SpheralGnuPlotUtilities import multiSort
    state = State(db, integrator.physicsPackages())
    outputFile = os.path.join(dataDir, outputFile)
    pos = state.vectorFields(HydroFieldNames.position)
    rho = state.scalarFields(HydroFieldNames.massDensity)
    P = state.scalarFields(HydroFieldNames.pressure)
    vel = state.vectorFields(HydroFieldNames.velocity)
    eps = state.scalarFields(HydroFieldNames.specificThermalEnergy)
    Hfield = state.symTensorFields(HydroFieldNames.H)
    S = state.symTensorFields(SolidFieldNames.deviatoricStress)
    ps = state.scalarFields(SolidFieldNames.plasticStrain)
    rprof = mpi.reduce([x.magnitude() for x in internalValues(pos)], mpi.SUM)
    rhoprof = mpi.reduce(internalValues(rho), mpi.SUM)
    Pprof = mpi.reduce(internalValues(P), mpi.SUM)
    vprof = mpi.reduce([v.x for v in internalValues(vel)], mpi.SUM)
    epsprof = mpi.reduce(internalValues(eps), mpi.SUM)
    hprof = mpi.reduce([2.0/(H.Trace()) for H in internalValues(Hfield)], mpi.SUM)
    sprof = mpi.reduce([x.xx for x in internalValues(S)], mpi.SUM)
    psprof = mpi.reduce(internalValues(ps), mpi.SUM)
    mof = mortonOrderIndices(db)
    mo = mpi.reduce(internalValues(mof), mpi.SUM)
    if mpi.rank == 0:
        multiSort(mo, rprof, rhoprof, Pprof, vprof, epsprof, hprof, sprof, psprof)
        f = open(outputFile, "w")
        f.write(("#" + 17*' "%16s"' + "\n") % ("r", "rho", "P", "v", "eps", "h", "S", "plastic strain", "m", 
                                               "int(r)", "int(rho)", "int(P)", "int(v)", "int(eps)", "int(h)", "int(S)", "int(ps)"))
        for (ri, rhoi, Pi, vi, epsi, hi, si, psi, mi) in zip(rprof, rhoprof, Pprof, vprof, epsprof, hprof, sprof, psprof, mo):
            f.write((8*"%16.12e " + 9*"%i " + "\n") %
                    (ri, rhoi, Pi, vi, epsi, hi, si, psi, mi,
                     unpackElementUL(packElementDouble(ri)),
                     unpackElementUL(packElementDouble(rhoi)),
                     unpackElementUL(packElementDouble(Pi)),
                     unpackElementUL(packElementDouble(vi)),
                     unpackElementUL(packElementDouble(epsi)),
                     unpackElementUL(packElementDouble(hi)),
                     unpackElementUL(packElementDouble(si)),
                     unpackElementUL(packElementDouble(psi))))
        f.close()

        #---------------------------------------------------------------------------
        # Also we can optionally compare the current results with another file.
        #---------------------------------------------------------------------------
        if comparisonFile != "None":
            comparisonFile = os.path.join(dataDir, comparisonFile)
            import filecmp
            assert filecmp.cmp(outputFile, comparisonFile)

#-------------------------------------------------------------------------------
# Plot the state.
#-------------------------------------------------------------------------------
if graphics:
    from SpheralGnuPlotUtilities import *
    state = State(db, integrator.physicsPackages())
    rhoPlot = plotFieldList(state.scalarFields("mass density"),
                            xFunction = "%s.x",
                            plotStyle="points",
                            winTitle="rho @ %g" % (control.time()))
    velPlot = plotFieldList(state.vectorFields("velocity"),
                            xFunction = "%s.x",
                            yFunction = "%s.x",
                            plotStyle="points",
                            winTitle="vel @ %g" % (control.time()))
    mPlot = plotFieldList(state.scalarFields("mass"),
                          xFunction = "%s.x",
                          plotStyle="points",
                          winTitle="mass @ %g" % (control.time()))
    PPlot = plotFieldList(state.scalarFields("pressure"),
                          xFunction = "%s.x",
                          plotStyle="points",
                          winTitle="pressure @ %g" % (control.time()))
    hPlot = plotFieldList(state.symTensorFields("H"),
                          xFunction = "%s.x",
                          yFunction = "1.0/%s.xx",
                          plotStyle="points",
                          winTitle="h @ %g" % (control.time()))
    psPlot = plotFieldList(state.scalarFields(SolidFieldNames.plasticStrain),
                           xFunction = "%s.x",
                           plotStyle="points",
                           winTitle="plastic strain @ %g" % (control.time()))

#-------------------------------------------------------------------------------
# Check the answer
#-------------------------------------------------------------------------------
if checkError:
    if abs(rsim0 - rInnerCheck) > rInnerError:
        raise RuntimeError, "Inner shell radius %g outside expected range [%g:%g]" % (rsim0, rInnerCheck - rInnerError, rInnerCheck + rInnerError)
    if compatibleEnergy and Eerror > 1.0e-10:
        raise RuntimeError, "Energy error %g > %g" % (Eerror, 1.0e-10)
