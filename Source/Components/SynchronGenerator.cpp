#include "SynchronGenerator.h"

using namespace DPsim;

SynchronGenerator::SynchronGenerator(std::string name, int node1, int node2, int node3,
	SynchGenStateType stateType, double nomPower, double nomVolt, double nomFreq, int poleNumber, double nomFieldCur,
	SynchGenParamType paramType, double Rs, double Ll, double Lmd, double Lmd0, double Lmq, double Lmq0,
	double Rfd, double Llfd, double Rkd, double Llkd,
	double Rkq1, double Llkq1, double Rkq2, double Llkq2,
	double inertia) {

	this->node1 = node1 - 1;
	this->node2 = node2 - 1;
	this->node3 = node3 - 1;

	mStateType = stateType;
	mNomPower = nomPower;
	mNomVolt = nomVolt;
	mNomFreq = nomFreq;
	mPoleNumber = poleNumber;
	mNomFieldCur = nomFieldCur;

	// base stator values
	mBase_V_RMS = mNomVolt / sqrt(3);
	mBase_v = mBase_V_RMS * sqrt(2);
	mBase_I_RMS = mNomPower / (3 * mBase_V_RMS);
	mBase_i = mBase_I_RMS * sqrt(2);
	mBase_Z = mBase_v / mBase_i;
	mBase_OmElec = 2 * DPS_PI * mNomFreq;
	mBase_OmMech = mBase_OmElec / (mPoleNumber / 2);
	mBase_L = mBase_Z / mBase_OmElec;
	mBase_Psi = mBase_L * mBase_i;
	mBase_T = mNomPower / mBase_OmMech;

	if (paramType == SynchGenParamType::perUnit) {
		// steady state per unit initial value
		initWithPerUnitParam(Rs, Ll, Lmd, Lmd0, Lmq, Lmq0, Rfd, Llfd, Rkd, Llkd, Rkq1, Llkq1, Rkq2, Llkq2, inertia);
	}

}

void SynchronGenerator::initWithPerUnitParam(
	double Rs, double Ll, double Lmd, double Lmd0, double Lmq, double Lmq0,
	double Rfd, double Llfd, double Rkd, double Llkd,
	double Rkq1, double Llkq1, double Rkq2, double Llkq2,
	double H) {

	// base rotor values
	mBase_ifd = Lmd * mNomFieldCur;
	mBase_vfd = mNomPower / mBase_ifd;
	mBase_Zfd = mBase_vfd / mBase_ifd;
	mBase_Lfd = mBase_Zfd / mBase_OmElec;

	if (mStateType == SynchGenStateType::perUnit) {
		mRs = Rs;
		mLl = Ll;
		mLmd = Lmd;
		mLmd0 = Lmd0;
		mLmq = Lmq;
		mLmq0 = Lmq0;
		mRfd = Rfd;
		mLlfd = Llfd;
		mRkd = Rkd;
		mLlkd = Llkd;
		mRkq1 = Rkq1;
		mLlkq1 = Llkq1;
		mRkq2 = Rkq2;
		mLlkq2 = Llkq2;
		mH = H;
		// Additional inductances according to Krause
		mLaq = 1 / (1 / mLmq + 1 / mLl + 1 / mLlkq1 + 1 / mLlkq2);
		mLad = 1 / (1 / mLmd + 1 / mLl + 1 / mLlkd + 1 / mLlfd);
	}
	else if (mStateType == SynchGenStateType::statorReferred) {
		mRs = Rs * mBase_Z;
		mLl = Ll * mBase_L;
		mLmd = Lmd * mBase_L;
		mLmd0 = Lmd0 * mBase_L;
		mLmq = Lmq * mBase_L;
		mLmq0 = Lmq0 * mBase_L;
		mRfd = Rfd * mBase_Z;
		mLlfd = Llfd * mBase_L;
		mRkd = Rkd * mBase_Z;
		mLlkd = Llkd * mBase_L;
		mRkq1 = Rkq1 * mBase_Z;
		mLlkq1 = Llkq1 * mBase_L;
		mRkq2 = Rkq2 * mBase_Z;
		mLlkq2 = Llkq2 * mBase_L;
		// Additional inductances according to Krause
		mLaq = 1 / (1 / mLmq + 1 / mLl + 1 / mLlkq1 + 1 / mLlkq2) * mBase_L;
		mLad = 1 / (1 / mLmd + 1 / mLl + 1 / mLlkd + 1 / mLlfd) * mBase_L;
	}
}

void SynchronGenerator::init(double om, double dt,
	double initActivePower, double initReactivePower, double initTerminalVolt, double initVoltAngle) {

	// Create matrices for state space representation 
	mInductanceMat <<
		mLl + mLmq, 0, 0, mLmq, mLmq, 0, 0,
		0, mLl + mLmd, 0, 0, 0, mLmd, mLmd,
		0, 0, mLl, 0, 0, 0, 0,
		mLmq, 0, 0, mLlkq1 + mLmq, mLmq, 0, 0,
		mLmq, 0, 0, mLmq, mLlkq2 + mLmq, 0, 0,
		0, mLmd, 0, 0, 0, mLlfd + mLmd, mLmd,
		0, mLmd, 0, 0, 0, mLmd, mLlkd + mLmd;

	mResistanceMat <<
		mRs, 0, 0, 0, 0, 0, 0,
		0, mRs, 0, 0, 0, 0, 0,
		0, 0, mRs, 0, 0, 0, 0,
		0, 0, 0, mRkq1, 0, 0, 0,
		0, 0, 0, 0, mRkq2, 0, 0,
		0, 0, 0, 0, 0, mRfd, 0,
		0, 0, 0, 0, 0, 0, mRkd;

	mOmegaFluxMat <<
		0, 1, 0, 0, 0, 0, 0,
		-1, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0;

	mReverseCurrents <<
		-1, 0, 0, 0, 0, 0, 0,
		0, -1, 0, 0, 0, 0, 0,
		0, 0, -1, 0, 0, 0, 0,
		0, 0, 0, 1, 0, 0, 0,
		0, 0, 0, 0, 1, 0, 0,
		0, 0, 0, 0, 0, 1, 0,
		0, 0, 0, 0, 0, 0, 1;

	mReactanceMat = mInductanceMat.inverse();

	if (mStateType == SynchGenStateType::perUnit) {
		// steady state per unit initial value
		initStatesInPerUnit(initActivePower, initReactivePower, initTerminalVolt, initVoltAngle);
	}
	else if (mStateType == SynchGenStateType::statorReferred) {
		// steady state stator referred initial value
		//InitStatesInStatorRefFrame(initActivePower, initReactivePower, initTerminalVolt, initVoltAngle);
	}

	mDq0Voltages(0, 0) = mVoltages(0, 0);
	mDq0Voltages(1, 0) = mVoltages(1, 0);
	mDq0Voltages(2, 0) = mVoltages(2, 0);
	mDq0Voltages = mDq0Voltages * mBase_v;
	mAbcsVoltages = dq0ToAbcTransform(mThetaMech, mDq0Voltages);

	mDq0Currents(0, 0) = mCurrents(0, 0);
	mDq0Currents(1, 0) = mCurrents(1, 0);
	mDq0Currents(2, 0) = mCurrents(2, 0);
	mDq0Currents = mDq0Currents * mBase_i;
	mAbcsCurrents = dq0ToAbcTransform(mThetaMech, mDq0Currents);
}

void SynchronGenerator::initStatesInPerUnit(double initActivePower, double initReactivePower,
	double initTerminalVolt, double initVoltAngle) {

	double init_P = initActivePower / mNomPower;
	double init_Q = initReactivePower / mNomPower;
	double init_S = sqrt(pow(init_P, 2.) + pow(init_Q, 2.));
	double init_vt = initTerminalVolt / mBase_v;
	double init_it = init_S / init_vt;

	// power factor
	double init_pf = acos(init_P / init_S);

	// load angle
	double init_delta = atan(((mLmq + mLl) * init_it * cos(init_pf) - mRs * init_it * sin(init_pf)) /
		(init_vt + mRs * init_it * cos(init_pf) + (mLmq + mLl) * init_it * sin(init_pf)));
	double init_delta_deg = init_delta / DPS_PI * 180;

	// dq stator voltages and currents
	double init_vd = init_vt * sin(init_delta);
	double init_vq = init_vt * cos(init_delta);
	double init_id = init_it * sin(init_delta + init_pf);
	double init_iq = init_it * cos(init_delta + init_pf);

	// rotor voltage and current
	double init_ifd = (init_vq + mRs * init_iq + (mLmd + mLl) * init_id) / mLmd;
	double init_vfd = mRfd * init_ifd;

	// flux linkages
	double init_psid = init_vq + mRs * init_iq;
	double init_psiq = -init_vd - mRs * init_id;
	double init_psifd = (mLmd + mLlfd) * init_ifd - mLmd * init_id;
	double init_psid1 = mLmd * (init_ifd - init_id);
	double init_psiq1 = -mLmq * init_iq;
	double init_psiq2 = -mLmq * init_iq;

	// rotor mechanical variables
	double init_Te = init_P + mRs * pow(init_it, 2.);
	mOmMech = 1;

	mVoltages(0, 0) = init_vq;
	mVoltages(1, 0) = init_vd;
	mVoltages(2, 0) = 0;
	mVoltages(3, 0) = 0;
	mVoltages(4, 0) = 0;
	mVoltages(5, 0) = init_vfd;
	mVoltages(6, 0) = 0;

	mCurrents(0, 0) = init_iq;
	mCurrents(1, 0) = init_id;
	mCurrents(2, 0) = 0;
	mCurrents(3, 0) = 0;
	mCurrents(4, 0) = 0;
	mCurrents(5, 0) = init_ifd;
	mCurrents(6, 0) = 0;

	mFluxes(0, 0) = init_psiq;
	mFluxes(1, 0) = init_psid;
	mFluxes(2, 0) = 0;
	mFluxes(3, 0) = init_psiq1;
	mFluxes(4, 0) = init_psiq2;
	mFluxes(5, 0) = init_psifd;
	mFluxes(6, 0) = init_psid1;

	// Initialize mechanical angle
	mThetaMech = initVoltAngle + init_delta - PI / 2.;
}

void SynchronGenerator::step(DPSMatrix& g, DPSMatrix& j, int compOffset, double om, double dt, double t,
	double fieldVoltage, double mechPower) {

	if (mStateType == SynchGenStateType::perUnit) {
		stepInPerUnit(om, dt, t, fieldVoltage, mechPower);
	}
	else if (mStateType == SynchGenStateType::statorReferred) {
		//StepInStatorRefFrame(om, dt, t, fieldVoltage, mechPower);
	}

	// Update current source accordingly
	if (node1 >= 0) {
		j(node1, 0) = j(node1, 0) + mAbcsCurrents(0, 0);
		j(compOffset + node1, 0) = j(compOffset + node1, 0) + mAbcsCurrents(3, 0);
	}
	if (node2 >= 0) {
		j(node2, 0) = j(node2, 0) + mAbcsCurrents(1, 0);
		j(compOffset + node2, 0) = j(compOffset + node2, 0) + mAbcsCurrents(4, 0);
	}
	if (node3 >= 0) {
		j(node3, 0) = j(node3, 0) + mAbcsCurrents(2, 0);
		j(compOffset + node3, 0) = j(compOffset + node3, 0) + mAbcsCurrents(5, 0);
	}
}

void SynchronGenerator::stepInPerUnit(double om, double dt, double t, double fieldVoltage, double mechPower) {
	// retrieve voltages
	mAbcsVoltages = (1 / mBase_v) * mAbcsVoltages;
	mAbcsCurrents = (1 / mBase_i) * mAbcsCurrents;
	// mVoltages(5, 0) = fieldVoltage / mBase_v;
	// TODO calculate effect of changed field voltage

	// dq-transform of interface voltage
	mDq0Voltages = abcToDq0Transform(mThetaMech, mAbcsVoltages);
	mVoltages(0, 0) = mDq0Voltages(0, 0);
	mVoltages(1, 0) = mDq0Voltages(1, 0);
	mVoltages(2, 0) = mDq0Voltages(2, 0);

	// calculate mechanical states
	mMechPower = mechPower / mNomPower;
	mMechTorque = mMechPower / mOmMech;
	mElecTorque = (mFluxes(1, 0)*mCurrents(0, 0) - mFluxes(0, 0)*mCurrents(1, 0));

	// Euler step forward	
	mOmMech = mOmMech + dt * (1 / (2 * mH) * (mMechTorque - mElecTorque));
	Matrix currents = mReverseCurrents * mReactanceMat * mFluxes;
	DPSMatrix dtFluxes = mVoltages - mResistanceMat * currents - mOmMech * mOmegaFluxMat * mFluxes;
	mFluxes = mFluxes + dt * mBase_OmElec * dtFluxes;

	mCurrents = mReverseCurrents * mReactanceMat * mFluxes;

	// inverse dq-transform
	mDq0Currents(0, 0) = mCurrents(0, 0);
	mDq0Currents(1, 0) = mCurrents(1, 0);
	mDq0Currents(2, 0) = mCurrents(2, 0);
	mAbcsCurrents = dq0ToAbcTransform(mThetaMech, mDq0Currents);
	mAbcsCurrents = mBase_i * mAbcsCurrents;

	// Update mechanical rotor angle with respect to electrical angle
	mThetaMech = mThetaMech + dt * ((mOmMech - 1) * mBase_OmMech);
}

void SynchronGenerator::postStep(DPSMatrix& g, DPSMatrix& j, DPSMatrix& vt, int compOffset, double om, double dt, double t) {
	if (node1 >= 0) {
		mAbcsVoltages(0, 0) = vt(node1, 0);
		mAbcsVoltages(3, 0) = vt(node1 + compOffset, 0);
	}
	else {
		mAbcsVoltages(0, 0) = 0;
		mAbcsVoltages(3, 0) = 0;
	}
	if (node2 >= 0) {
		mAbcsVoltages(1, 0) = vt(node2, 0);
		mAbcsVoltages(4, 0) = vt(node2 + compOffset, 0);
	}
	else {
		mAbcsVoltages(1, 0) = 0;
		mAbcsVoltages(4, 0) = 0;
	}
	if (node3 >= 0) {
		mAbcsVoltages(2, 0) = vt(node3, 0);
		mAbcsVoltages(5, 0) = vt(node3 + compOffset, 0);
	}
	else {
		mAbcsVoltages(2, 0) = 0;
		mAbcsVoltages(5, 0) = 0;
	}
}

DPSMatrix SynchronGenerator::abcToDq0Transform(Real theta, DPSMatrix& in) {
	// Balanced case
	Complex alpha(cos(2. / 3. * PI), sin(2. / 3. * PI));	
	Complex thetaCompInv(cos(-theta), sin(-theta));
	MatrixComp AbcToPnz(3, 3);
	AbcToPnz <<
		1, 1, 1,
		1, alpha, pow(alpha, 2),
		1, pow(alpha, 2), alpha;
	AbcToPnz = (1. / 3.) * AbcToPnz;

	MatrixComp abcVector(3, 1);
	abcVector <<
		Complex(in(0, 0), in(3, 0)),
		Complex(in(1, 0), in(4, 0)),
		Complex(in(2, 0), in(5, 0));

	MatrixComp pnzVector(3, 1);
	pnzVector = AbcToPnz * abcVector * thetaCompInv;

	DPSMatrix dq0Vector(3, 1);
	dq0Vector <<
		pnzVector(1, 0).imag(),
		pnzVector(1, 0).real(),
		0;
	
	return dq0Vector;
}

DPSMatrix SynchronGenerator::dq0ToAbcTransform(Real theta, DPSMatrix& in) {
	// Balanced case
	Complex alpha(cos(2. / 3. * PI), sin(2. / 3. * PI));
	Complex thetaComp(cos(theta), sin(theta));
	MatrixComp PnzToAbc(3, 3);
	PnzToAbc <<
		1, 1, 1,
		1, pow(alpha, 2), alpha,
		1, alpha, pow(alpha, 2);

	MatrixComp pnzVector(3, 1);
	pnzVector <<
		0,
		Complex(in(1, 0), in(0, 0)),
		Complex(0, 0);

	MatrixComp abcCompVector(3, 1);
	abcCompVector = PnzToAbc * pnzVector * thetaComp;
	
	Matrix abcVector(6, 1);
	abcVector <<
		abcCompVector(0, 0).real(),
		abcCompVector(1, 0).real(),
		abcCompVector(2, 0).real(),
		abcCompVector(0, 0).imag(),
		abcCompVector(1, 0).imag(),
		abcCompVector(2, 0).imag();

	return abcVector;
}
