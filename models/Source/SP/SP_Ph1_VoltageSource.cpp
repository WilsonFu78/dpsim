/* Copyright 2017-2021 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/

#include <cps/SP/SP_Ph1_VoltageSource.h>

using namespace CPS;

SP::Ph1::VoltageSource::VoltageSource(String uid, String name, Logger::Level logLevel)
	: SimPowerComp<Complex>(uid, name, logLevel),
	mVoltageRef(Attribute<Complex>::createDynamic("V_ref", mAttributes)),
	mSrcFreq(Attribute<Real>::createDynamic("f_src", mAttributes)) {
	setVirtualNodeNumber(1);
	setTerminalNumber(2);
	**mIntfVoltage = MatrixComp::Zero(1, 1);
	**mIntfCurrent = MatrixComp::Zero(1, 1);
}

SimPowerComp<Complex>::Ptr SP::Ph1::VoltageSource::clone(String name) {
	auto copy = VoltageSource::make(name, mLogLevel);
	if(mSrcSig == 0)
		copy->setParameters(**mVoltageRef);
	else
		copy->setParameters(mSrcSig->getSignal());
	return copy;
}

void SP::Ph1::VoltageSource::setParameters(Complex voltageRef, Real srcFreq) {
	auto srcSigSine = Signal::SineWaveGenerator::make(**mName + "_sw");
	srcSigSine->setParameters(voltageRef, srcFreq);
	mSrcSig = srcSigSine;

	mVoltageRef->setReference(mSrcSig->mSigOut);
	mSrcFreq->setReference(mSrcSig->mFreq);

	mParametersSet = true;
}

void SP::Ph1::VoltageSource::setParameters(Complex initialPhasor, Real freqStart, Real rocof, Real timeStart, Real duration, bool useAbsoluteCalc) {
	auto srcSigFreqRamp = Signal::FrequencyRampGenerator::make(**mName + "_fr");
	srcSigFreqRamp->setParameters(initialPhasor, freqStart, rocof, timeStart, duration, useAbsoluteCalc);
	mSrcSig = srcSigFreqRamp;

	mVoltageRef->setReference(mSrcSig->mSigOut);
	mSrcFreq->setReference(mSrcSig->mFreq);

	mParametersSet = true;
}

void SP::Ph1::VoltageSource::setParameters(Complex initialPhasor, Real modulationFrequency, Real modulationAmplitude, Real baseFrequency, bool zigzag) {
    auto srcSigFm = Signal::CosineFMGenerator::make(**mName + "_fm");
	srcSigFm->setParameters(initialPhasor, modulationFrequency, modulationAmplitude, baseFrequency, zigzag);
	mSrcSig = srcSigFm;

	mVoltageRef->setReference(mSrcSig->mSigOut);
	mSrcFreq->setReference(mSrcSig->mFreq);

	mParametersSet = true;
}

void SP::Ph1::VoltageSource::initializeFromNodesAndTerminals(Real frequency) {
	Complex voltageRef = **mVoltageRef;

	if (voltageRef == Complex(0, 0))
		voltageRef = initialSingleVoltage(1) - initialSingleVoltage(0);

	if (mSrcSig == nullptr) {
		Signal::SineWaveGenerator srcSigSine(**mName);
		srcSigSine.setParameters(voltageRef);
		mSrcSig = std::make_shared<Signal::SineWaveGenerator>(srcSigSine);

		mVoltageRef->setReference(mSrcSig->mSigOut);
	mSrcFreq->setReference(mSrcSig->mFreq);
	} else {
		**mVoltageRef = voltageRef;
	}

	mSLog->info(
		"\n--- Initialization from node voltages ---"
		"\nVoltage across: {:s}"
		"\nTerminal 0 voltage: {:s}"
		"\nTerminal 1 voltage: {:s}"
		"\n--- Initialization from node voltages ---",
		Logger::phasorToString(mSrcSig->getSignal()),
		Logger::phasorToString(initialSingleVoltage(0)),
		Logger::phasorToString(initialSingleVoltage(1)));
}

// #### MNA functions ####

void SP::Ph1::VoltageSource::mnaAddPreStepDependencies(AttributeBase::List &prevStepDependencies, AttributeBase::List &attributeDependencies, AttributeBase::List &modifiedAttributes) {
	attributeDependencies.push_back(mVoltageRef);
	modifiedAttributes.push_back(mRightVector);
	modifiedAttributes.push_back(mIntfVoltage);
}

void SP::Ph1::VoltageSource::mnaAddPostStepDependencies(AttributeBase::List &prevStepDependencies, AttributeBase::List &attributeDependencies, AttributeBase::List &modifiedAttributes, Attribute<Matrix>::Ptr &leftVector) {
	attributeDependencies.push_back(leftVector);
	modifiedAttributes.push_back(mIntfCurrent);
};

void SP::Ph1::VoltageSource::mnaInitialize(Real omega, Real timeStep, Attribute<Matrix>::Ptr leftVector) {
	MNAInterface::mnaInitialize(omega, timeStep);
	updateMatrixNodeIndices();

	(**mIntfVoltage)(0,0) = mSrcSig->getSignal();
	mMnaTasks.push_back(std::make_shared<MnaPreStep>(*this));
	mMnaTasks.push_back(std::make_shared<MnaPostStep>(*this, leftVector));
	**mRightVector = Matrix::Zero(leftVector->get().rows(), 1);

	mSLog->info(
		"\n--- MNA initialization ---"
		"\nInitial voltage {:s}"
		"\nInitial current {:s}"
		"\n--- MNA initialization finished ---",
		Logger::phasorToString((**mIntfVoltage)(0,0)),
		Logger::phasorToString((**mIntfCurrent)(0,0)));
}

void SP::Ph1::VoltageSource::mnaApplySystemMatrixStamp(Matrix& systemMatrix) {
	for (UInt freq = 0; freq < mNumFreqs; freq++) {
		if (terminalNotGrounded(0)) {
			Math::setMatrixElement(systemMatrix, mVirtualNodes[0]->matrixNodeIndex(), matrixNodeIndex(0), Complex(-1, 0), mNumFreqs, freq);
			Math::setMatrixElement(systemMatrix, matrixNodeIndex(0), mVirtualNodes[0]->matrixNodeIndex(), Complex(-1, 0), mNumFreqs, freq);
		}
		if (terminalNotGrounded(1)) {
			Math::setMatrixElement(systemMatrix, mVirtualNodes[0]->matrixNodeIndex(), matrixNodeIndex(1), Complex(1, 0), mNumFreqs, freq);
			Math::setMatrixElement(systemMatrix, matrixNodeIndex(1), mVirtualNodes[0]->matrixNodeIndex(), Complex(1, 0), mNumFreqs, freq);
		}

		mSLog->info("-- Stamp frequency {:d} ---", freq);
		if (terminalNotGrounded(0)) {
			mSLog->info("Add {:f} to system at ({:d},{:d})", -1., matrixNodeIndex(0), mVirtualNodes[0]->matrixNodeIndex());
			mSLog->info("Add {:f} to system at ({:d},{:d})", -1., mVirtualNodes[0]->matrixNodeIndex(), matrixNodeIndex(0));
		}
		if (terminalNotGrounded(1)) {
			mSLog->info("Add {:f} to system at ({:d},{:d})", 1., mVirtualNodes[0]->matrixNodeIndex(), matrixNodeIndex(1));
			mSLog->info("Add {:f} to system at ({:d},{:d})", 1., matrixNodeIndex(1), mVirtualNodes[0]->matrixNodeIndex());
		}
	}
}

void SP::Ph1::VoltageSource::mnaApplyRightSideVectorStamp(Matrix& rightVector) {
	// TODO: Is this correct with two nodes not gnd?
	Math::setVectorElement(rightVector, mVirtualNodes[0]->matrixNodeIndex(), (**mIntfVoltage)(0,0), mNumFreqs);
	SPDLOG_LOGGER_DEBUG(mSLog, "Add {:s} to source vector at {:d}",
		Logger::complexToString((**mIntfVoltage)(0,0)), mVirtualNodes[0]->matrixNodeIndex());
}

void SP::Ph1::VoltageSource::updateVoltage(Real time) {
	if(mSrcSig != nullptr) {
		mSrcSig->step(time);
		(**mIntfVoltage)(0,0) = mSrcSig->getSignal();
	} else {
		(**mIntfVoltage)(0,0) = **mVoltageRef;
	}

	mSLog->debug("Update Voltage {:s}", Logger::phasorToString((**mIntfVoltage)(0,0)));
}

void SP::Ph1::VoltageSource::mnaPreStep(Real time, Int timeStepCount) {
	updateVoltage(time);
	mnaApplyRightSideVectorStamp(**mRightVector);
}

void SP::Ph1::VoltageSource::mnaPostStep(Real time, Int timeStepCount, Attribute<Matrix>::Ptr &leftVector) {
	mnaUpdateCurrent(**leftVector);
}

void SP::Ph1::VoltageSource::mnaUpdateCurrent(const Matrix& leftVector) {
	for (UInt freq = 0; freq < mNumFreqs; freq++) {
		(**mIntfCurrent)(0,freq) = Math::complexFromVectorElement(leftVector, mVirtualNodes[0]->matrixNodeIndex(), mNumFreqs, freq);
	}
}

void SP::Ph1::VoltageSource::daeResidual(double ttime, const double state[], const double dstate_dt[], double resid[], std::vector<int>& off) {
	/* new state vector definintion:
		state[0]=node0_voltage
		state[1]=node1_voltage
		....
		state[n]=noden_voltage
		state[n+1]=component0_voltage
		state[n+2]=component0_inductance (not yet implemented)
		...
		state[m-1]=componentm_voltage
		state[m]=componentm_inductance
	*/

	//int Pos1 = matrixNodeIndex(0);
	//int Pos2 = matrixNodeIndex(1);
	//int c_offset = off[0] + off[1]; //current offset for component
	//int n_offset_1 = c_offset + Pos1 + 1;// current offset for first nodal equation
	//int n_offset_2 = c_offset + Pos2 + 1;// current offset for second nodal equation
	//resid[c_offset] = (state[Pos2] - state[Pos1]) - state[c_offset]; // Voltage equation for Resistor
	////resid[++c_offset] = ; //TODO : add inductance equation
	//resid[n_offset_1] += std::real(current());
	//resid[n_offset_2] += std::real(current());
	//off[1] += 1;
}

Complex SP::Ph1::VoltageSource::daeInitialize() {
	(**mIntfVoltage)(0, 0) = mSrcSig->getSignal();
	return mSrcSig->getSignal();
}
