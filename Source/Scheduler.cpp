/* Copyright 2017-2021 Institute for Automation of Complex Power Systems,
 *                     EONERC, RWTH Aachen University
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *********************************************************************************/

#include <dpsim/Scheduler.h>

#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

using namespace CPS;
using namespace DPsim;

CPS::AttributeBase::Ptr Scheduler::external;

void Scheduler::initMeasurements(const Task::List& tasks) {
	// Fill map here already since it's not protected by a mutex
	for (auto task : tasks) {
		mMeasurements[task.get()] = std::vector<TaskTime>();
	}
}

void Scheduler::updateMeasurement(Task* ptr, TaskTime time) {
	mMeasurements[ptr].push_back(time);
}

void Scheduler::writeMeasurements(String filename) {
	std::ofstream os(filename);
	std::unordered_map<String, TaskTime> averages;
	for (auto& pair : mMeasurements) {
		averages[pair.first->toString()] = getAveragedMeasurement(pair.first);
	}
	// TODO think of nicer output format
	for (auto pair : averages) {
		os << pair.first << "," << pair.second.count() << std::endl;
	}
	os.close();
}

void Scheduler::readMeasurements(String filename, std::unordered_map<String, TaskTime::rep>& measurements) {
	std::ifstream fs(filename);
	if (!fs.good())
		throw SchedulingException();

	String name;
	while (fs.good()) {
		std::string line;
		std::getline(fs, line);
		int idx = static_cast<UInt>(line.find(','));
		if (idx == -1) {
			if (line.empty())
				continue;
			throw SchedulingException();
		}
		measurements[line.substr(0, idx)] = std::stol(line.substr(idx+1));
	}
}

Scheduler::TaskTime Scheduler::getAveragedMeasurement(CPS::Task* task) {
	TaskTime avg(0), tot(0);

	for (TaskTime time : mMeasurements[task]) {
		tot += time;
	}

	if (!mMeasurements[task].empty())
		avg = tot / mMeasurements[task].size();

	return avg;
}


void Scheduler::resolveDeps(Task::List& tasks, Edges& inEdges, Edges& outEdges) {
	// Create graph (list of out/in edges for each node) from attribute dependencies
	tasks.push_back(mRoot);
	std::unordered_map<AttributeBase::Ptr, std::deque<Task::Ptr>, std::hash<AttributeBase::Ptr>, CPS::AttributeEq<AttributeBase>> dependencies;
	std::unordered_set<AttributeBase::Ptr, std::hash<AttributeBase::Ptr>, CPS::AttributeEq<AttributeBase>> prevStepDependencies;
	for (auto task : tasks) {
		for (AttributeBase::Ptr attr : task->getAttributeDependencies()) {
			/// CHECK: Having external be the nullptr can lead to segfaults rather quickly. Maybe make it a special kind of attribute
			if (attr.getPtr() != Scheduler::external.getPtr()) {
				AttributeBase::Set attrDependencies = attr->getDependencies();
				for (AttributeBase::Ptr dep : attrDependencies) {
					dependencies[dep].push_back(task);
				}
			} else {
				dependencies[attr].push_back(task);
			}
		}
		for (AttributeBase::Ptr attr : task->getPrevStepDependencies()) {
			prevStepDependencies.insert(attr);
		}
	}

	for (auto from : tasks) {
		for (AttributeBase::Ptr attr : from->getModifiedAttributes()) {
			for (auto to : dependencies[attr]) {
				outEdges[from].push_back(to);
				inEdges[to].push_back(from);
			}
			if (prevStepDependencies.count(attr)) {
				outEdges[from].push_back(mRoot);
				inEdges[mRoot].push_back(from);
			}
		}
	}
}

void Scheduler::topologicalSort(const Task::List& tasks, const Edges& inEdges, const Edges& outEdges, Task::List& sortedTasks) {
	sortedTasks.clear();

	// make copies of the edge lists because we modify them (and it makes
	// things easier since empty lists are automatically created)
	Edges inEdgesCpy = inEdges, outEdgesCpy = outEdges;

	// do a breadth-first search backwards from the root node first to filter
	// out unnecessary nodes
	std::deque<Task::Ptr> q;
	std::unordered_set<Task::Ptr> visited;

	q.push_back(mRoot);
	while (!q.empty()) {
		auto t = q.front();
		q.pop_front();
		if (visited.count(t))
			continue;

		visited.insert(t);
		for (auto dep : inEdgesCpy[t]) {
			if (!visited.count(dep)) {
				q.push_back(dep);
			}
		}
	}

	for (auto t : tasks) {
		if (inEdgesCpy[t].empty()) {
			q.push_back(t);
		}
	}
	// keep list of tasks without incoming edges;
	// iteratively remove such tasks from the graph and put them into the schedule
	while (!q.empty()) {
		Task::Ptr t = q.front();
		q.pop_front();
		if (!visited.count(t)) {
			// don't put unneeded tasks in the schedule, but process them as usual
			// so the cycle check still works
			mSLog->info("Dropping {:s}", t->toString());
		} else if (t != mRoot) {
			sortedTasks.push_back(t);
		}

		for (auto after : outEdgesCpy[t]) {
			for (auto edgeIt = inEdgesCpy[after].begin(); edgeIt != inEdgesCpy[after].end(); ++edgeIt) {
				if (*edgeIt == t) {
					inEdgesCpy[after].erase(edgeIt);
					break;
				}
			}
			if (inEdgesCpy[after].empty()) {
				q.push_back(after);
			}
		}
		outEdgesCpy.erase(t);
	}

	// sanity check: all edges should have been removed, otherwise
	// the graph had a cycle
	for (auto t : tasks) {
		if (!outEdgesCpy[t].empty() || !inEdgesCpy[t].empty())
			throw SchedulingException();
	}

}

void Scheduler::levelSchedule(const Task::List& tasks, const Edges& inEdges, const Edges& outEdges, std::vector<Task::List>& levels) {
	std::unordered_map<Task::Ptr, int> time;

	for (auto task : tasks) {
		if (inEdges.find(task) == inEdges.end() || inEdges.at(task).empty()) {
			time[task] = 0;
		} else {
			int maxdist = 0;
			for (auto before : inEdges.at(task)) {
				if (time[before] > maxdist)
					maxdist = time[before];
			}
			time[task] = maxdist + 1;
		}
	}

	levels.clear();
	levels.resize(time[tasks.back()] + 1);
	for (auto task : tasks) {
		levels[time[task]].push_back(task);
	}
}

void BarrierTask::addBarrier(Barrier* b) {
	mBarriers.push_back(b);
}

void BarrierTask::execute(Real time, Int timeStepCount) {
	if (mBarriers.size() == 1) {
		mBarriers[0]->wait();
	} else {
		for (size_t i = 0; i < mBarriers.size()-1; i++) {
			mBarriers[i]->signal();
		}
		mBarriers[mBarriers.size()-1]->wait();
	}
}
