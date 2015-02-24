/*
 * CopyingCollector.hpp
 *
 *  Created on: 2013-09-04
 *      Author: GarCoSim
 */

#ifndef CopyingCollector_HPP_
#define CopyingCollector_HPP_

#include "Allocator.hpp"
#include "Collector.hpp"
#include "ObjectContainer.hpp"
#include <queue>

using namespace std;

namespace traceFileSimulator {

class MemoryManager;

class CopyingCollector : public Collector {
public:
	CopyingCollector();
	virtual ~CopyingCollector();
	void collect(int reason);
//	void checkWatermark();
	void printStats();
	
private:
	void mark();
	void sweep();
	void compact();
	void enqueueAllRoots();
	void initializeMarkPhase();
	void preCollect();
	void postCollect();
	void freeAllLiveObjects();
	void reallocateAllLiveObjects();
};

} 
#endif /* CopyingCollector_HPP_ */