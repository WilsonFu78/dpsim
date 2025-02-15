/* Copyright 2017-2021 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/

#include <cps/DP/DP_Ph1_VoltageSourceRamp.h>

using namespace CPS;

DP::Ph1::VoltageSourceRamp::VoltageSourceRamp(String uid, String name,
	Logger::Level logLevel)
	: SimPowerComp<Complex>(uid, name, logLevel),
	mVoltageRef(Attribute<Complex>::create("V_ref", mAttributes)),
	mSrcFreq(Attribute<Real>::create("f_src", mAttributes)) {
	setVirtualNodeNumber(1);
	setTerminalNumber(2);
	**mIntfVoltage = MatrixComp::Zero(1,1);
	**mIntfCurrent = MatrixComp::Zero(1,1);
}

/// DEPRECATED: Delete method
SimPowerComp<Complex>::Ptr DP::Ph1::VoltageSourceRamp::clone(String name) {
	auto copy = VoltageSourceRamp::make(name, mLogLevel);
	copy->setParameters(**mVoltageRef, mAddVoltage, **mSrcFreq, mAddSrcFreq, mSwitchTime, mRampTime);
	return copy;
}

void DP::Ph1::VoltageSourceRamp::setParameters(Complex voltage, Complex addVoltage, Real srcFreq,
	Real addSrcFreq, Real switchTime, Real rampTime) {
	**mVoltageRef = voltage;
	mAddVoltage = addVoltage;
	**mSrcFreq = srcFreq;
	mAddSrcFreq = addSrcFreq;
	mSwitchTime = switchTime;
	mRampTime = rampTime;

	mParametersSet = true;
}

void DP::Ph1::VoltageSourceRamp::initialize(Matrix frequencies) {
	SimPowerComp<Complex>::initialize(frequencies);

	if (**mVoltageRef == Complex(0, 0))
		**mVoltageRef = initialSingleVoltage(1) - initialSingleVoltage(0);

	mSubVoltageSource = VoltageSource::make(**mName + "_src", mLogLevel);
	mSubVoltageSource->setParameters(**mVoltageRef, **mSrcFreq);
	mSubVoltageSource->connect({ node(0), node(1) });
	mSubVoltageSource->setVirtualNodeAt(mVirtualNodes[0], 0);
	mSubVoltageSource->initialize(frequencies);
}

void DP::Ph1::VoltageSourceRamp::initializeFromNodesAndTerminals(Real frequency) {
	mSubVoltageSource->initializeFromNodesAndTerminals(frequency);
}

void DP::Ph1::VoltageSourceRamp::mnaInitialize(Real omega, Real timeStep, Attribute<Matrix>::Ptr leftVector) {
	MNAInterface::mnaInitialize(omega, timeStep);
	updateMatrixNodeIndices();
	mSubVoltageSource->mnaInitialize(omega, timeStep, leftVector);
	// only need a new MnaPreStep that updates the reference voltage of mSubVoltageSource;
	// its own tasks then do the rest
	/// FIXME: Can we avoid setting right_vector to dynamic?
	mRightVector->setReference(mSubVoltageSource->mRightVector);
	mMnaTasks.push_back(std::make_shared<MnaPreStep>(*this));
	for (auto task : mSubVoltageSource->mnaTasks()) {
		mMnaTasks.push_back(task);
	}
}

void DP::Ph1::VoltageSourceRamp::mnaApplySystemMatrixStamp(Matrix& systemMatrix) {
	mSubVoltageSource->mnaApplySystemMatrixStamp(systemMatrix);
}

void DP::Ph1::VoltageSourceRamp::mnaApplyRightSideVectorStamp(Matrix& rightVector) {
	mSubVoltageSource->mnaApplyRightSideVectorStamp(rightVector);
}

void DP::Ph1::VoltageSourceRamp::updateState(Real time) {
	(**mIntfVoltage)(0,0) = **mVoltageRef;

	if (time >= mSwitchTime && time < mSwitchTime + mRampTime) {
		Real voltageAbs = Math::abs(**mVoltageRef + (time - mSwitchTime) / mRampTime * mAddVoltage);
		Real voltagePhase = Math::phase(**mVoltageRef + (time - mSwitchTime) / mRampTime * mAddVoltage);
		Real fadeInOut = 0.5 + 0.5 * sin((time - mSwitchTime) / mRampTime * PI + -PI / 2);
		(**mIntfVoltage)(0,0) = Math::polar(voltageAbs, voltagePhase + fadeInOut * mAddSrcFreq * time);
	}
	else if (time >= mSwitchTime + mRampTime) {
		Real voltageAbs = Math::abs(**mVoltageRef + mAddVoltage);
		Real voltagePhase = Math::phase(**mVoltageRef + mAddVoltage);
		(**mIntfVoltage)(0,0) = Math::polar(voltageAbs, voltagePhase + mAddSrcFreq * time);
	}
}

void DP::Ph1::VoltageSourceRamp::MnaPreStep::execute(Real time, Int timeStepCount) {
	mVoltageSource.updateState(time);
	**mVoltageSource.mSubVoltageSource->mVoltageRef = (**mVoltageSource.mIntfVoltage)(0, 0);
}
