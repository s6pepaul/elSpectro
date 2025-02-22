#include "DecayModelst.h"
#include "SDMEDecay.h"
#include "FunctionsForGenvector.h"
#include <TDatabasePDG.h>
#include <Math/GSLIntegrator.h>
#include <Math/IntegrationTypes.h>
#include <Math/Functor.h>
#include <Math/Minimizer.h>
#include <Math/Factory.h>
#include <TRandom3.h>

namespace elSpectro{
  ///////////////////////////////////////////////////////
  ///constructor includes subseqent decay of Ngamma* system
  DecayModelst::DecayModelst(particle_ptrs parts, const std::vector<int> pdgs) :
    DecayModel{ parts, pdgs }
  {
    _name={"DecayModelst"};

    //need to find meson and baryon
    if(TDatabasePDG::Instance()->GetParticle(Products()[0]->Pdg())->ParticleClass()==TString("Baryon") ){
      _baryon=Products()[0];
      _meson=Products()[1]; 
    }
    else {
      _baryon=Products()[1];
      _meson=Products()[0];
    }
    
  }
  /////////////////////////////////////////////////////////////////
  void DecayModelst::PostInit(ReactionInfo* info){
    std::cout<<"DecayModelst::PostInit  "<<std::endl;
    if( dynamic_cast<DecayingParticle*>(_meson) ){
      if( dynamic_cast<DecayingParticle*>(_meson)->Model()->CanUseSDME() ){
	auto sdmeModel=dynamic_cast<SDMEDecay*>(dynamic_cast<DecayingParticle*>(_meson)->Model());
      _sdmeMeson = _meson->InitSDME(sdmeModel->Spin(),4); //4=>photoproduction
	//could have electroproduced baryon spin 3/2
	//_sdmeBaryon = _baryon->InitSDME(3,9);
      }
    }
      std::cout<<"DecayModelst::PostInit  "<<std::endl;
  
    DecayModel::PostInit(info);
   std::cout<<"DecayModelst::PostInit  "<<std::endl;
 
    _isElProd=kTRUE;
    _prodInfo= dynamic_cast<ReactionElectroProd*> (info); //I need Reaction info
    if(_prodInfo==nullptr){
      _isElProd=kFALSE;
      _prodInfo= dynamic_cast<ReactionPhotoProd*> (info);
    }
    _photon = _prodInfo->_photon;
    _target = _prodInfo->_target;
    _ebeam = _prodInfo->_ebeam;
    _photonPol = _prodInfo->_photonPol;
    
     double maxW = ( *(_prodInfo->_target) + *(_prodInfo->_ebeam) ).M();

     _max = FindMaxOfIntensity()*1.08; //add 5% for Q2,meson mass effects etc.

     std::cout<<"DecayModelst::PostInit max value "<<_max<<" "<<_meson<<" "<<_meson->Pdg()<<" "<<_sdmeMeson<<std::endl;
  }
  
  //////////////////////////////////////////////////////////////////
  double DecayModelst::Intensity() const
  {
    /*A      B        A/2    B/2        A*2/3   B   
      1      2         1/2    1          1/3    1   30each    5     10       
      1/2    1   -->   1/4    1/2  --->  1/6    1/2  ---->    5/2   5
      0      2         0      1                 1   row       0     10

     */
    _W = Parent()->P4().M();
    _s=_W*_W;
    _t = (_meson->P4()-*_photon).M2();//_amp->kinematics->t_man(s,cmMeson.Theta());
    _dt=0;
    _dsigma=0;
    //check above threshold for meson and baryon masses
    if( _W < (_meson->P4().M()+_baryon->P4().M()) ) return 0;
    //std::cout<<"DecayModelst "<<Parent()->Pdg()<<" "<<_meson->P4().M()<<" "<<_baryon->P4().M()<<std::endl;

    if(_isElProd==kTRUE){
      //now we can define production/polarisation plane
      MomentumVector decayAngles;
      kine::electroCMDecay(&Parent()->P4(),_ebeam,_photon,_meson->P4ptr(),&decayAngles);
      _photonPol->SetPhi(decayAngles.Phi());
    }
    else{//photoproduction
      _photonPol->SetPhi(_meson->P4().Phi());
    }

    //now kinemaics
    _dt=4* TMath::Sqrt(PgammaCMsq())  * kine::PDK(_W,_meson->P4().M(),_baryon->P4().M() );
      
    double weight = DifferentialXSect() * _dt ;//must multiply by t-range for correct sampling
  
    CalcMesonSDMEs();
    CalcBaryonSDMEs();

    weight/=_max; //normalise range 0-1
    if(_isElProd==kTRUE)
      weight/= TMath::Sqrt(PgammaCMsq()/kine::PDK2(_W,0,_target->M())); //correct max for finite Q2 phase space
 
    if(weight>1){
      //don't change weight, likely due to large Q2 value....
      std::cout<<"DecayModelst::Intensity weight too high but won't change maxprobable low meson mass and W from  "<<_max<<" to "<<weight*_max<<" meson "<<_meson->Mass()<<" W "<<_W<<std::endl;
      }
    
    //Correct for W weighting which has already been applied
    weight/=_prodInfo->_sWeight;
    // std::cout<<" s weight "<<_prodInfo->_sWeight<<" weight "<<weight<<" "<<_W<<std::endl;
    if(weight>1){
      std::cout<<" s weight "<<_prodInfo->_sWeight<<" Q2 "<<-_photon->M2()<<" 2Mmu "<<2*_target->M()*_photon->E() <<" W "<<_W<<" t "<<_t<<" new weight "<<weight*_prodInfo->_sWeight<<" meson "<<_meson->Mass()<<std::endl;
      std::cout<<"DecayModelst::Intensity sWeight corrected weight too large "<<weight <<" "<<_prodInfo->_sWeight<<"  max "<<_max<<" val "<< weight*_prodInfo->_sWeight*_max<<std::endl;
      std::cout<<"DX "<<DifferentialXSect()<<" "<< _dt<<" pgam "<<TMath::Sqrt(PgammaCMsq())<<" M^2 "<<MatrixElementsSquared_T()<<" Q2 factor "<<TMath::Sqrt(PgammaCMsq())/kine::PDK(_W,0,_baryon->Mass())<<" PHASE SPACE "<<PhaseSpaceFactor()<<" s "<<_s<<" pgam "<<PgammaCMsq()<<" at Q2 0  = "<<kine::PDK2(_W,0,_target->M())<<std::endl;
      }
     
 
    return weight;
    
  }
  
  double DecayModelst::FindMaxOfIntensity(){
    
    auto M1 = 0;//assum real photon for max calculation
    auto M2 = _target->M();
    auto M3 = _meson->Mass(); //should be pdg value here
    auto M4 = _baryon->Mass();
    auto Wmin = Parent()->MinimumMassPossible();
   
    //    _Wmax = ( *(_prodInfo->_target) + *(_prodInfo->_ebeam) ).M();
    _Wmax = _prodInfo->_Wmax;
    
    std::cout<<" DecayModelst::FindMaxOfIntensity()  Wmin = "<<Wmin<<" Wmax = "<<_Wmax<<" meson mass = "<<M3<<" baryon mass = "<<M4<<" target mass = "<<M2<<std::endl;

    auto Fmax = [&M1,&M2,&M3,&M4,&Wmin,this](const double *x)
      {
	_s = x[0]*x[0];
	_W=x[0];
	if( _W < Wmin ) return 0.;
	if( _W < M3+M4 ) return 0.;
	if( _W > _Wmax ) return 0.;

	auto currt=kine::tFromcosthW(x[1],_W,M1,M2,M3,M4);
	auto myt0=kine::t0(_W,M1,M2,M3,M4);
	auto mytmax=kine::tmax(_W,M1,M2,M3,M4);
	if(currt>myt0) return 0.;
	if(currt<mytmax) return 0.;
	if( TMath::IsNaN(x[1]) ) return 0.;

	_t=currt;
	auto dt=4* TMath::Sqrt(PgammaCMsq())  * kine::PDK(_W,M3,M4 );

	double val = DifferentialXSect()*dt;
	if( TMath::IsNaN(val) ) return 0.;
	return -(val); //using a minimiser!
      };
   
      //First perform grid search for intital values
      double Wrange=_Wmax-Wmin;
      double tmax=kine::tmax(_Wmax,M1,M2,M3,M4);
       
      double gridMin=0;
      double gridW=0;
      double gridt=0;
      double WtVals[2];
   
      ROOT::Math::Minimizer* minimum =
	ROOT::Math::Factory::CreateMinimizer("Genetic", "");
      //	ROOT::Math::Factory::CreateMinimizer("Minuit2", "Combined");

      if(minimum==nullptr) //Minuit2 not always installed!
	minimum = ROOT::Math::Factory::CreateMinimizer("Minuit", "");
      
      // set tolerance , etc...
      minimum->SetMaxFunctionCalls(1000000); // for Minuit/Minuit2
      minimum->SetMaxIterations(1000);  // for GSL
      minimum->SetTolerance(0.0001);
      minimum->SetPrintLevel(0);
      
      // create function wrapper for minimizer
      // a IMultiGenFunction type
      ROOT::Math::Functor wrapf(Fmax,2);

      //variable = W, variable 1 = t
      // double step[2] = {Wrange/500,0.001};
      double step[2] = {Wrange/100,2./100};
      // starting point
      
      // double variable[2] = { gridW,gridt};
      double variable[2] = {Wmin+Wrange/2,0};
      
      minimum->SetFunction(wrapf);
      
      // Set the free variables to be minimized !
      minimum->SetVariable(0,"W",variable[0], step[0]);
      minimum->SetVariable(1,"t",variable[1], step[1]);
      // minimum->SetVariableLimits(0,Wmin,_Wmax);
      // minimum->SetVariableLimits(1,tmax,0);

      // do the minimization
      minimum->Minimize();
      const double *xs = minimum->X();
 
      auto minVal = minimum->MinValue();
      auto minW= xs[0];
      auto mint= kine::tFromcosthW(xs[1],minW,M1,M2,M3,M4);//xs[1];
   
      std::cout << "Maximum : Probabiltiy Dist at ( W=" << minW << " , t = "  << mint << "): "<< -minimum->MinValue()  << " note t0 "<<kine::t0(minW,M1,M2,M3,M4)<< std::endl;

      //check for low mass meson limits
      if(dynamic_cast<DecayingParticle*>(_meson)){ //meson
	dynamic_cast<DecayingParticle*>(_meson)->TakeMinimumMass();//to get threshold behaviour
	M3=_meson->Mass();
	
	// do the minimization at min mass in case higher max
	minimum->Minimize();
	const double *xs = minimum->X();
	
	auto minminVal = minimum->MinValue();
	

	if(minminVal<minVal){
	  std::cout << "Minimum Mass Maximum : Probabiltiy Dist at ( W=" << minW << " , t = "  << mint << "): "<< -minimum->MinValue()<< " note t0 "<<kine::t0(minW,M1,M2,M3,M4)  << std::endl;
	  std::cout<<"minmin "<<-minminVal<<" < "<<-minVal<<std::endl;
	  minVal=minminVal;
	  minW= xs[0];
	  mint= xs[1];
	}
	//back to PDg mass if exists
	if(_meson->PdgMass()>M3)
	  M3=_meson->PdgMass();
	  dynamic_cast<DecayingParticle*>(_meson)->TakePdgMass();

      }

      if(gridMin<minVal){
	Warning("DecayModelst::FindMaxOfIntensity()","grid search value already bigger than minimised, so will revert to that max value +5 percent");
	std::cout<<"gridMin "<<gridMin<<" "<<minVal<<std::endl;
	minVal=gridMin*1.05;
      }

      return -minVal;
  }
  /*
  void DecayModelst::HistIntegratedXSection(TH1D& hist){

 
    auto F = [this](double t)
      {
	//_W=W;
	_s=_W*_W;
	_t=t;
	return DifferentialXSect();
      };
    
      ROOT::Math::GSLIntegrator ig(ROOT::Math::IntegrationOneDim::kADAPTIVE,
				   ROOT::Math::Integration::kGAUSS61);
      ROOT::Math::Functor1D wF(F);
      ig.SetFunction(wF);

      
      auto M1 = 0;//assum real photon for calculation
      auto M2 = _target->M();
      auto M3 = _meson->Mass(); //should be pdg value here
      auto M4 = _baryon->Mass();
      auto Wmin = M3+M4;

      for(int ih=1;ih<=hist.GetNbinsX();ih++){
	_W=hist.GetXaxis()->GetBinCenter(ih);
	//	_W=_W+hist.GetXaxis()->GetBinWidth(ih); //take right limit so do not miss threshold
	if( _W < Wmin )
	  hist.SetBinContent(ih, 0);
	if( TMath::IsNaN(kine::tmax(_W,M1,M2,M3,M4)) )
	  hist.SetBinContent(ih, 0);
	if( TMath::IsNaN(kine::t0(_W,M1,M2,M3,M4)) )
	  hist.SetBinContent(ih, 0);
	else
	  hist.SetBinContent(ih, ig.Integral(kine::tmax(_W,M1,M2,M3,M4),kine::t0(_W,M1,M2,M3,M4)) );
	
	std::cout<<ih<<" "<<"Going to integrate from t "<<kine::tmax(_W,M1,M2,M3,M4)<<" "<<kine::t0(_W,M1,M2,M3,M4)<<" at W "<<" and "<<_W<<" "<<Wmin<<" with result "<<hist.GetBinContent(ih)<<"    t range "<< kine::tmax(_W,M1,M2,M3,M4) - kine::t0(_W,M1,M2,M3,M4)<<std::endl;
	//or try histogram method
	//TH1F histi("histi","integral",100, kine::tmax(_W,M1,M2,M3,M4),kine::t0(_W,M1,M2,M3,M4));
	//for(int it=1;it<=histi.GetNbinsX();it++){histi.SetBinContent(it,F(histi.GetBinCenter(it)));}
	//std::cout<<"alternative "<<histi.Integral("width")<<std::endl;
	if(ih%10==0)std::cout<<(hist.GetNbinsX() - ih)/10<<" "<<std::endl;
      }
      std::cout<<std::endl;
      //done
  }
  */
  void DecayModelst::HistIntegratedXSection(TH1D& hist){

 
    auto M1 = 0;//assume real photon for calculation
    auto M2 = _target->M();
    auto M3 = _meson->Mass(); //should be pdg value here
    auto M4 = _baryon->Mass();
    auto Wmin = M3+M4;
 
    //integrate over costh
    auto F = [this,M1,M2,M3,M4](double costh)
      {
	_s=_W*_W;
	_t = kine::tFromcosthW(costh, _W, M1, M2, M3, M4);
	return PhaseSpaceFactorCosTh()* (MatrixElementsSquared_T());
     };
    
      ROOT::Math::GSLIntegrator ig(ROOT::Math::IntegrationOneDim::kADAPTIVE,
				   ROOT::Math::Integration::kGAUSS61);
      ROOT::Math::Functor1D wF(F);
      ig.SetFunction(wF);

      
  
      for(int ih=1;ih<=hist.GetNbinsX();ih++){
	_W=hist.GetXaxis()->GetBinCenter(ih);
	if( _W < Wmin )
	  hist.SetBinContent(ih, 0);
	else
	  hist.SetBinContent(ih, ig.Integral(-1,1) );
	
    }
      std::cout<<std::endl;
      //done
  }
  
 void DecayModelst::HistIntegratedXSection_ds(TH1D& hist){

 
    auto M1 = 0;//assum real photon for calculation
    auto M2 = _target->M();
    auto M3 = _meson->Mass(); //should be pdg value here
    auto M4 = _baryon->Mass();
    auto Wmin = M3+M4;

    auto F = [this,M1,M2,M3,M4](double t)
      {
	//_W=W;
	_s=_W*_W;
	//_t=t;
	//_W = Parent()->P4().M();
	_t = kine::tFromcosthW(t, _W, M1, M2, M3, M4);
	return PhaseSpaceFactorCosTh()* (MatrixElementsSquared_T());
     };
    
      ROOT::Math::GSLIntegrator ig(ROOT::Math::IntegrationOneDim::kADAPTIVE,
				   ROOT::Math::Integration::kGAUSS61);
      ROOT::Math::Functor1D wF(F);
      ig.SetFunction(wF);

      
  
      for(int ih=1;ih<=hist.GetNbinsX();ih++){
	_W=sqrt(hist.GetXaxis()->GetBinCenter(ih));
	//	_W=_W+hist.GetXaxis()->GetBinWidth(ih); //take right limit so do not miss threshold
	if( _W < Wmin )
	  hist.SetBinContent(ih, 0);
	else
	  hist.SetBinContent(ih, ig.Integral(-1,1) );
	
	if(ih%10==0)std::cout<<(hist.GetNbinsX() - ih)/10<<" "<<std::endl;
      }
      std::cout<<std::endl;
      //done
  }
  
  void DecayModelst::HistMaxXSection(TH1D& hist){

 
    auto M1 = 0;//assum real photon for calculation
    auto M2 = _target->M();
    auto M3 = _meson->Mass(); //should be pdg value here
    auto M4 = _baryon->Mass();
    //auto Wmin = M3+M4;
    auto Wmin = Parent()->MinimumMassPossible();
 
    auto F = [this,&Wmin](double t)
      {
	//_W=W;
	if(_W<Wmin)return 0.;
	_s=_W*_W;
	_t=t;
	return DifferentialXSect();
      };
    
   
      
 
      for(int ih=1;ih<=hist.GetNbinsX();ih++){
	_W=hist.GetXaxis()->GetBinCenter(ih);
	if( _W < Wmin )
	  hist.SetBinContent(ih, 0);
	else if( TMath::IsNaN(kine::tmax(_W,M1,M2,M3,M4)) )
	  hist.SetBinContent(ih, 0);
	else if( TMath::IsNaN(kine::t0(_W,M1,M2,M3,M4)) )
	  hist.SetBinContent(ih, 0);
	else{
	  double max_at_W=0;
	  double tmax=kine::tmax(_W,M1,M2,M3,M4);
	  double tmin=kine::t0(_W,M1,M2,M3,M4);
	  int Ntpoints=100;
	  double tstep=(tmax-tmin)/Ntpoints;
	  double tval=tmin;
	  for(int itt=0;itt<Ntpoints;itt++){
	    _W=hist.GetXaxis()->GetBinCenter(ih);
	    
	    double val_at_t = F(tval)*(tmin-tmax);
	    if(val_at_t>max_at_W)
	      max_at_W=val_at_t;

	    
	    _W=_W+hist.GetXaxis()->GetBinWidth(ih)/2; //take right limit
	    val_at_t = F(tval)*(tmin-tmax);
	    if(val_at_t>max_at_W)
	      max_at_W=val_at_t;
	    
	    _W=_W-hist.GetXaxis()->GetBinWidth(ih); //take left limit 
	    val_at_t = F(tval)*(tmin-tmax);
	    if(val_at_t>max_at_W)
	      max_at_W=val_at_t;
	    
	    //move on
	    tval+=tstep;
	  }
	  hist.SetBinContent(ih, max_at_W );

	}
      }
      //	if(ih%10==0)std::cout<<(hist.GetNbinsX() - ih)/10<<" "<<std::endl;
  
      std::cout<<std::endl;
      //done
  }
}
/* perhaps this can go in script for fixed values of sdmes
    //Meson spin density marix elements, note this is photoproduced
    if(_sdmeMeson){
      _sdmeMeson->SetElement(0,0,0,);
      _sdmeMeson->SetElement(0,1,0,);
      _sdmeMeson->SetElement(0,1,-1,);
      _sdmeMeson->SetElement(1,1,1,);
      _sdmeMeson->SetElement(1,0,0,);
      _sdmeMeson->SetElement(1,1,0,);
      _sdmeMeson->SetElement(1,1,-1,);
      _sdmeMeson->SetElement(2,1,0,);
      _sdmeMeson->SetElement(2,1,-1,);
      //_sdmeMeson->SetElement(3,1,0,(_amp->SDME(3, 1, 0, s, t)));
      // _sdmeMeson->SetElement(3,1,-1,(_amp->SDME(3, 1, -1, s, t)));
    }
    */
