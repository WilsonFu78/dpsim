/* Copyright 2017-2021 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/

#include <cps/EMT/EMT_Ph3_NetworkInjection.h>

using namespace CPS;

EMT::Ph3::NetworkInjection::NetworkInjection(String uid, String name, Logger::Level logLevel)
	: SimPowerComp<Real>(uid, name, logLevel),
	mVoltageRef(Attribute<MatrixComp>::createDynamic("V_ref", mAttributes)),
	mSrcFreq(Attribute<Real>::createDynamic("f_src", mAttributes)),
	mSigOut(Attribute<Complex>::createDynamic("sigOut", mAttributes))  {
	mPhaseType = PhaseType::ABC;
	setVirtualNodeNumber(0);
	setTerminalNumber(1);
	**mIntfVoltage = Matrix::Zero(3, 1);
	**mIntfCurrent = Matrix::Zero(3, 1);

	mSLog->info("Create {} {}", this->type(), name);

	// Create electrical sub components
	mSubVoltageSource = std::make_shared<EMT::Ph3::VoltageSource>(**mName + "_vs", mLogLevel);
	mSubComponents.push_back(mSubVoltageSource);
	mSLog->info("Electrical subcomponents: ");
	for (auto subcomp: mSubComponents)
		mSLog->info("- {}", subcomp->name());

	mVoltageRef->setReference(mSubVoltageSource->mVoltageRef);
	mSrcFreq->setReference(mSubVoltageSource->mSrcFreq);
	mSigOut->setReference(mSubVoltageSource->mSigOut);
}

SimPowerComp<Real>::Ptr EMT::Ph3::NetworkInjection::clone(String name) {
	auto copy = NetworkInjection::make(name, mLogLevel);
	copy->setParameters(**mVoltageRef);
	return copy;
}

void EMT::Ph3::NetworkInjection::setParameters(MatrixComp voltageRef, Real srcFreq) {
	mParametersSet = true;

	mSubVoltageSource->setParameters(voltageRef, srcFreq);

	///FIXME: This should not be necessary, because the reference is already set in the constructor
	mVoltageRef->setReference(mSubVoltageSource->mVoltageRef);
	mSrcFreq->setReference(mSubVoltageSource->mSrcFreq);

	mSLog->info("\nVoltage Ref={:s} [V]"
				"\nFrequency={:s} [Hz]",
				Logger::matrixCompToString(voltageRef),
				Logger::realToString(srcFreq));
}

void EMT::Ph3::NetworkInjection::setParameters(MatrixComp voltageRef, Real freqStart, Real rocof, Real timeStart, Real duration, bool useAbsoluteCalc) {
	mParametersSet = true;

	mSubVoltageSource->setParameters(voltageRef, freqStart, rocof, timeStart, duration, useAbsoluteCalc);

	///FIXME: This should not be necessary, because the reference is already set in the constructor
	mVoltageRef->setReference(mSubVoltageSource->mVoltageRef);
	mSrcFreq->setReference(mSubVoltageSource->mSrcFreq);
	mSigOut->setReference(mSubVoltageSource->mSigOut);

	mSLog->info("\nVoltage Ref={:s} [V]"
				"\nFrequency={:s} [Hz]",
				Logger::matrixCompToString(voltageRef),
				Logger::realToString(freqStart));
}

void EMT::Ph3::NetworkInjection::setParameters(MatrixComp voltageRef, Real modulationFrequency, Real modulationAmplitude, Real baseFrequency /*= 0.0*/, bool zigzag /*= false*/) {
	mParametersSet = true;

	mSubVoltageSource->setParameters(voltageRef, modulationFrequency, modulationAmplitude, baseFrequency, zigzag);

	///FIXME: This should not be necessary, because the reference is already set in the constructor
	mVoltageRef->setReference(mSubVoltageSource->mVoltageRef);
	mSrcFreq->setReference(mSubVoltageSource->mSrcFreq);

	mSLog->info("\nVoltage Ref={:s} [V]"
				"\nFrequency={:s} [Hz]",
				Logger::matrixCompToString(voltageRef),
				Logger::realToString(baseFrequency));
}

void EMT::Ph3::NetworkInjection::initializeFromNodesAndTerminals(Real frequency) {
	// Connect electrical subcomponents
	mSubVoltageSource->connect({ SimNode::GND, node(0) });

	// Initialize electrical subcomponents
	for (auto subcomp: mSubComponents) {
		subcomp->initialize(mFrequencies);
		subcomp->initializeFromNodesAndTerminals(frequency);
	}

	///FIXME: This should not be necessary, because the reference is already set in the constructor
	mVoltageRef->setReference(mSubVoltageSource->mVoltageRef);
	mSrcFreq->setReference(mSubVoltageSource->mSrcFreq);
}

// #### MNA functions ####

void EMT::Ph3::NetworkInjection::mnaInitialize(Real omega, Real timeStep, Attribute<Matrix>::Ptr leftVector) {
	MNAInterface::mnaInitialize(omega, timeStep);
	updateMatrixNodeIndices();

	// initialize electrical subcomponents
	for (auto subcomp: mSubComponents)
		if (auto mnasubcomp = std::dynamic_pointer_cast<MNAInterface>(subcomp))
			mnasubcomp->mnaInitialize(omega, timeStep, leftVector);

	// collect right side vectors of subcomponents
	mRightVectorStamps.push_back(&**mSubVoltageSource->mRightVector);

	// collect tasks
	mMnaTasks.push_back(std::make_shared<MnaPreStep>(*this));
	mMnaTasks.push_back(std::make_shared<MnaPostStep>(*this, leftVector));

	**mRightVector = Matrix::Zero(leftVector->get().rows(), 1);
}

void EMT::Ph3::NetworkInjection::mnaApplySystemMatrixStamp(Matrix& systemMatrix) {
	for (auto subcomp: mSubComponents)
		if (auto mnasubcomp = std::dynamic_pointer_cast<MNAInterface>(subcomp))
			mnasubcomp->mnaApplySystemMatrixStamp(systemMatrix);
}

void EMT::Ph3::NetworkInjection::mnaApplyRightSideVectorStamp(Matrix& rightVector) {
	rightVector.setZero();
	for (auto stamp : mRightVectorStamps)
		rightVector += *stamp;

	mSLog->debug("Right Side Vector: {:s}",
				Logger::matrixToString(rightVector));
}


void EMT::Ph3::NetworkInjection::mnaAddPreStepDependencies(AttributeBase::List &prevStepDependencies, AttributeBase::List &attributeDependencies, AttributeBase::List &modifiedAttributes) {
	// add pre-step dependencies of subcomponents
	for (auto subcomp: mSubComponents)
		if (auto mnasubcomp = std::dynamic_pointer_cast<MNAInterface>(subcomp))
			mnasubcomp->mnaAddPreStepDependencies(prevStepDependencies, attributeDependencies, modifiedAttributes);
	// add pre-step dependencies of component itself
	prevStepDependencies.push_back(mIntfCurrent);
	prevStepDependencies.push_back(mIntfVoltage);
	modifiedAttributes.push_back(mRightVector);
}

void EMT::Ph3::NetworkInjection::mnaPreStep(Real time, Int timeStepCount) {
	// pre-step of subcomponents
	for (auto subcomp: mSubComponents)
		if (auto mnasubcomp = std::dynamic_pointer_cast<MNAInterface>(subcomp))
			mnasubcomp->mnaPreStep(time, timeStepCount);
	// pre-step of component itself
	mnaApplyRightSideVectorStamp(**mRightVector);
}

void EMT::Ph3::NetworkInjection::mnaAddPostStepDependencies(AttributeBase::List &prevStepDependencies, AttributeBase::List &attributeDependencies, AttributeBase::List &modifiedAttributes, Attribute<Matrix>::Ptr &leftVector) {
	// add post-step dependencies of subcomponents
	for (auto subcomp: mSubComponents)
		if (auto mnasubcomp = std::dynamic_pointer_cast<MNAInterface>(subcomp))
			mnasubcomp->mnaAddPostStepDependencies(prevStepDependencies, attributeDependencies, modifiedAttributes, leftVector);
	// add post-step dependencies of component itself
	attributeDependencies.push_back(leftVector);
	modifiedAttributes.push_back(mIntfVoltage);
	modifiedAttributes.push_back(mIntfCurrent);
}

void EMT::Ph3::NetworkInjection::mnaPostStep(Real time, Int timeStepCount, Attribute<Matrix>::Ptr &leftVector) {
	// post-step of subcomponents
	for (auto subcomp: mSubComponents)
		if (auto mnasubcomp = std::dynamic_pointer_cast<MNAInterface>(subcomp))
			mnasubcomp->mnaPostStep(time, timeStepCount, leftVector);
	// post-step of component itself
	mnaUpdateCurrent(**leftVector);
	mnaUpdateVoltage(**leftVector);
}

void EMT::Ph3::NetworkInjection::mnaUpdateVoltage(const Matrix& leftVector) {
	**mIntfVoltage = **mSubVoltageSource->mIntfVoltage;
}

void EMT::Ph3::NetworkInjection::mnaUpdateCurrent(const Matrix& leftVector) {
	**mIntfCurrent = **mSubVoltageSource->mIntfCurrent;
}
