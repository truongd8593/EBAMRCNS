#ifdef CH_LANG_CC
/*
*      _______              __
*     / ___/ /  ___  __ _  / /  ___
*    / /__/ _ \/ _ \/  V \/ _ \/ _ \
*    \___/_//_/\___/_/_/_/_.__/\___/
*    Please refer to Copyright.txt, in Chombo's root directory.
*/
#endif

#include "ParmParse.H"
#include "CH_HDF5.H"
#include "parstream.H"

#include "EBInflowOutflowIBCFactory.H"
#include "EBPatchPolytropicFactory.H"

#include "EBPatchPolytropic.H"

#include "EBAMRCNSFactory.H"
#include "EBAMRCNS.H"
#include "AMRLevel.H"
#include "AMR.H"
#include "BaseIVFactory.H"
#include "EBIndexSpace.H"
#include "EBISLayout.H"
#include "BoxIterator.H"
#include "ParmParse.H"
#include "BRMeshRefine.H"
#include "LoadBalance.H"
#include "GeometryShop.H"
#include "PolyGeom.H"
#include "LevelData.H"
#include "EBCellFAB.H"
#include "EBCellFactory.H"
#include "VoFIterator.H"
#include "EBArith.H"
#include "AllRegularService.H"
#include "EBLevelRedist.H"
#include "RedistStencil.H"
#include "SlabService.H"
#include "DebugDump.H"
#include "EBDebugDump.H"
#include "EBFABView.H"
#include "memtrack.H"
#include "EBAMRCNSParams.H"
#include "GodunovGeom.H"
#include "EBViscousTensorOpFactory.H"
#include "DirichletPoissonEBBC.H"
#include   "NeumannPoissonEBBC.H"
#include "DirichletPoissonDomainBC.H"
#include   "NeumannPoissonDomainBC.H"
#include "DirichletViscousTensorEBBC.H"
#include   "NeumannViscousTensorEBBC.H"
#include "DirichletViscousTensorDomainBC.H"
#include   "NeumannViscousTensorDomainBC.H"
#include "DirichletConductivityDomainBC.H"
#include   "NeumannConductivityDomainBC.H"
#include "DirichletConductivityEBBC.H"
#include   "NeumannConductivityEBBC.H"
#include "CH_Attach.H"
#include "memusage.H"
#include <iostream>


using std::ifstream;
using std::ios;

void amrGodunov(const Box&      a_domain,
                const RealVect& a_dx)
{
  EBAMRCNSParams params;
  int iprob = 2; //magic number to get solver bcs
  fillAMRParams(params, iprob);
  
  ParmParse ppgodunov;
  bool tagAllIrregular = true;
  if(ppgodunov.contains("tag_all_irregular"))
    {
      ppgodunov.get("tag_all_irregular", tagAllIrregular);
    }
  EBAMRCNS::s_noEBCF = tagAllIrregular;

  Real gamma = 1.4;
  ppgodunov.get("gamma",gamma);

  Real specHeat = 1.0;
  ppgodunov.get("specific_heat",specHeat);

  Real tempInflow, tempInterior, presInflow, presInterior, machInflow;
  int flowDir;
  ppgodunov.get("inflow_temperature"  , tempInflow);
  ppgodunov.get("interior_temperature", tempInterior);
  ppgodunov.get("inflow_pressure"     , presInflow);
  ppgodunov.get("interior_pressure"   , presInterior);
  ppgodunov.get("inflow_mach"          , machInflow);
  flowDir = SpaceDim-1;
  ppgodunov.query("flow_direction"      , flowDir);

  EBInflowOutflowIBCFactory
    bcfactory(gamma, specHeat, tempInflow, tempInterior, presInflow, presInterior, machInflow, flowDir);

  //create patch integrator
  int ifourth, iflatten, iartvisc;
  ppgodunov.get("use_fourth_order_slopes", ifourth);
  ppgodunov.get("use_flattening"         , iflatten);
  ppgodunov.get("use_art_visc"           , iartvisc);
  bool useFourthOrderSlopes = (ifourth  == 1);
  bool useFlattening        = (iflatten == 1);
  bool useArtificialVisc    = (iartvisc == 1);
  bool useLimiting = true;
  bool doRZCoords  = false;
  RefCountedPtr<EBPatchPolytropicFactory> patchGamma = 
    RefCountedPtr<EBPatchPolytropicFactory>
    (new EBPatchPolytropicFactory(&bcfactory,
                                  gamma,
                                  specHeat,
                                  useFourthOrderSlopes,
                                  useFlattening,
                                  useArtificialVisc,
                                  useLimiting,
                                  doRZCoords));


  EBAMRCNSFactory amrg_fact(params,
                            patchGamma);

  AMR amr;
  setupAMR(amr, params, amrg_fact, a_domain, false, -1.0);

  // run
  int nstop = 0;
  ppgodunov.get("max_step",nstop);

  Real stopTime = 0.0;
  ppgodunov.get("max_time",stopTime);
  bool use_subcycling = true;
  ppgodunov.query("use_subcycling", use_subcycling);
  amr.useSubcyclingInTime(use_subcycling);

  amr.run(stopTime,nstop);

  // output last pltfile and statistics
  //cleanup
  amr.conclude();

}


/***************/
int
main(int a_argc, char* a_argv[])
{
#ifdef CH_MPI
  MPI_Init(&a_argc,&a_argv);
  // registerDebugger();
  // setChomboMPIErrorHandler();
#endif
  {
    // Check for an input file
    char* inFile = NULL;

    if (a_argc > 1)
      {
        inFile = a_argv[1];
      }
    else
      {
        pout() << "Usage: <executable name> <inputfile>" << endl;
        pout() << "No input file specified" << endl;
        return -1;
      }
    // Parse the command line and the input file (if any)
    ParmParse pp(a_argc-2,a_argv+2,NULL,inFile);

    Box coarsestDomain;
    RealVect dx;
    // run amrGodunov
    godunovGeometry(coarsestDomain, dx);
    print_memory_line("after geometry generation");

    amrGodunov(coarsestDomain, dx);
    print_memory_line("after big run");

    EBIndexSpace* ebisPtr = Chombo_EBIS::instance();
    ebisPtr->clear();
    print_memory_line("after everything");

    

  } //scoping trick
  pout() << "Done." << endl;

#ifdef CH_MPI
  CH_TIMER_REPORT();
  dumpmemoryatexit();
  MPI_Finalize();
#endif
}
