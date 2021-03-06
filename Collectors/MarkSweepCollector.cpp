/*
 * MarkSweepCollector.cpp
 *
 *  Created on: 2013-09-04
 *      Author: GarCoSim
 */

#include "MarkSweepCollector.hpp"

extern int gLineInTrace;
extern FILE* gLogFile;
extern FILE* gDetLog;

FILE* gcFile;

clock_t start, stop;

namespace traceFileSimulator {

MarkSweepCollector::MarkSweepCollector() {
}

/**
 * Argument indicates the reason for collection: 0 - unknown, 1 - failed alloc, 2 - high watermark
 */
void MarkSweepCollector::collect(int reason) {
	statCollectionReason = reason;
	preCollect();
	//easy as 1, 2, 3 :)
	mark();
	sweep();

	//comment in order to have mark/sweep only
	if (statFreedDuringThisGC > 0) {
		//only compact if you actually freed something
		compact();
	}

	postCollect();
}

void MarkSweepCollector::checkWatermark() {
	int size = myAllocator->getHeapSize();
	int free = myAllocator->getFreeSize();
	int ratio = 100 - (100 * free / size);
	if (ratio > myWatermark) {
		collect((int)reasonHighWatermark);
	}
}

void MarkSweepCollector::mark() {
	int i;
	//set all objects to dead and not visible firs
	initializeMarkPhase();

	enqueueAllRoots();

	//breadth first through the tree
	while (!myQueue.empty()) {
		Object* currentObj = myQueue.front();
		myQueue.pop();
		Object* child;
		int kids = currentObj->getPointersMax();
		currentObj->setVisited(true);
		currentObj->setAge(currentObj->getAge() + 1);
		for (i = 0; i < kids; i++) {
			child = currentObj->getReferenceTo(i);
			//no matter if the child was processed before or not, add it to the rem set.
			if(child && child->getGeneration() < currentObj->getGeneration()){
				myMemManager->requestRemSetAdd(child);
			}
			if (child && !child->getVisited() && child->getGeneration() <= myGeneration) {
				child->setVisited(true);

				myQueue.push(child);
			}
		}
	}

}

void MarkSweepCollector::sweep() {
	Object* currentObj;
	int i, gGC;
	vector<Object*> objects = myObjectContainer->getLiveObjects();
	for (i = 0; i < (int)objects.size(); i++) {
		currentObj = objects[i];
		if (currentObj) {
			//int id = currentObj->getID();
			if(myGeneration == GENERATIONS -1){
				gGC = 1;
			} else {
				gGC = 0;
			}
			if (currentObj && !currentObj->getVisited()) {
				myMemManager->requestDelete(currentObj, gGC);
				statFreedObjects++;
				statFreedDuringThisGC++;
			}
		}
	}
}


void MarkSweepCollector::enqueueAllRoots() {
	Object* currentObj;
	int i, j;
	if (myGeneration == GENERATIONS - 1) {
		//we are performing a glolab GC and can use it to fix possible rem set problems
		//we clear all rem sets and fill them again while performing the marking
		myMemManager->clearRemSets();

		vector<Object*> roots;
		for (i = 0; i < NUM_THREADS; i++) {
			roots = myObjectContainer->getRoots(i);
			for (j = 0; j < (int)roots.size(); j++) {
				currentObj = roots[j];
				if (currentObj && !currentObj->getVisited()) {
					currentObj->setVisited(true);
					//currentObj->setAge(currentObj->getAge() + 1);
					//add to rem set if the root is in a younger generation.
					if (currentObj->getGeneration() < myGeneration) {
						myMemManager->requestRemSetAdd(currentObj);
					}
					myQueue.push(currentObj);
				}
			}
		}
	} else {
		for (j = 0; j < myObjectContainer->getGenRootSize(); j++) {
			currentObj = myObjectContainer->getGenRoot(j);
			if (currentObj && !currentObj->getVisited()) {
				currentObj->setVisited(true);
				//currentObj->setAge(currentObj->getAge() + 1);
				myQueue.push(currentObj);
			}
		}
	}
}

void MarkSweepCollector::initializeMarkPhase() {
	Object* currentObj;
	int i;
	vector<Object*> objects = myObjectContainer->getLiveObjects();
	for (i = 0; i < (int)objects.size(); i++) {
		currentObj = objects[i];
		if (currentObj) {
			if (WRITE_DETAILED_LOG == 1) {
				//fprintf(gDetLog, "(%d) MARK: %ld\n", gLineInTrace,
						//(long) currentObj);
			}
			currentObj->setVisited(false);
		}
	}
}

void MarkSweepCollector::preCollect() {
	start = clock();
	statFreedDuringThisGC = 0;
	statGcNumber++;
//	if(myGeneration == GENERATIONS -1 ){
//		promotionPhase();
//	}
}

void MarkSweepCollector::compact() {
	/*only alive objects are left in the container. If I traverse
	 through the live list, I get all elements*/
	//free everything.
	myMemManager->statBeforeCompact(myGeneration);
	freeAllLiveObjects();
	//allocate everything back.
	myMemManager->requestResetAllocationPointer(myGeneration);
	reallocateAllLiveObjects();
	myMemManager->statAfterCompact(myGeneration);
}

void MarkSweepCollector::freeAllLiveObjects() {
	int i;
	//set all objects to dead and not visible firs
//	initializeMarkPhase();
//
//	enqueueAllRoots();
//
//	//breadth first through the tree
//	while (!myQueue.empty()) {
//		Object* currentObj = myQueue.front();
//		myQueue.pop();
//
//		//the actual work is just one line
//		myMemManager->requestFree(currentObj);
//
//		Object* child;
//		int kids = currentObj->getPointersMax();
//		currentObj->setVisited(true);
//		for (i = 0; i < kids; i++) {
//			child = currentObj->getReferenceTo(i);
//			if (child && !child->getVisited()) {
//				child->setVisited(true);
//				myQueue.push(child);
//			}
//		}
//	}
	vector<Object*> objects = myObjectContainer->getLiveObjects();
	for (i = 0; i < (int)objects.size(); i++) {
		Object* currentObj = objects[i];
		if (currentObj) {
			myMemManager->requestFree(currentObj);
		}
	}
	//myAllocator->freeAllSectors();
}

int MarkSweepCollector::promotionPhase() {
	if (gcsSinceLastPromotionPhase == 0) {
		//nothing to do here
		return 0;
	}
	int g, oldi;
	//in case the next generation is too full, a flag to wait
	int noSpaceUpstairs = 0;
	oldi = -1;
	vector<Object*> objects = myObjectContainer->getLiveObjects();
	for (g = 0; g < (int)objects.size(); g++) {
		Object* currentObj = objects[g];
		if (g < oldi) {
			printf("oO\n");
		}
		//if object exists and is not in the oldest generation already
		if (currentObj && currentObj->getGeneration() < (GENERATIONS - 1)) {
			int age = currentObj->getAge();
			if (age
					>= PROMOTIONAGE + PROMOTIONAGE * myGeneration
							/*+ currentObj->getGeneration() * PROMOTIONAGEFACTOR*/) {
				//fprintf(stderr, "(%d) promoting object of age %d\n",gLineInTrace, age);
				if (currentObj->getGeneration() < GENERATIONS - 1) {
					noSpaceUpstairs = myMemManager->requestPromotion(
							currentObj);
					// end the promotion phase because of an "out of memory"
					// exception in the higher generation. It is not the best
					// solution
					if (noSpaceUpstairs == 1) {
						return -1;
					}
				}
			}
		}
		oldi = g;
	}
	return 0;
}

void MarkSweepCollector::reallocateAllLiveObjects() {
	int i;
	//set all objects to dead and not visible firs
//	initializeMarkPhase();
//
//	enqueueAllRoots();
//
//	//breadth first through the tree
//	while (!myQueue.empty()) {
//		Object* currentObj = myQueue.front();
//		myQueue.pop();
//
//		//the actual work
//		myMemManager->requestReallocate(currentObj);
//
//		Object* child;
//		int kids = currentObj->getPointersMax();
//		currentObj->setVisited(true);
//		for (i = 0; i < kids; i++) {
//			child = currentObj->getReferenceTo(i);
//			if (child && !child->getVisited()) {
//				child->setVisited(true);
//				myQueue.push(child);
//			}
//		}
//	}
	vector<Object*> objects = myObjectContainer->getLiveObjects();
	for (i = 0; i < (int)objects.size(); i++) {
		Object* currentObj = objects[i];
		if (currentObj) {
			myMemManager->requestReallocate(currentObj);
		}
	}

}

MarkSweepCollector::~MarkSweepCollector() {
}

} 
