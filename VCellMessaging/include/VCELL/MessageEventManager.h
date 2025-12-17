//
// Created by Logan Drescher on 11/24/25.
//
#ifndef VCELL_ODE_NUMERICS_MESSAGEEVENTQUEUE_H
#define VCELL_ODE_NUMERICS_MESSAGEEVENTQUEUE_H

#include <queue>
#include <thread>
#include <mutex>
#include <functional>
#include <condition_variable>
#include "WorkerEvent.h"
/*
 * We want to avoid threads being idle, while processing updates
 * Components:
 *	1) An "active" boolean (locked by a mutex) that indicates whether the queue is being processed.
 *		1a) We must ensure that all relevant information a thread would use to ensure it's done, is always locked by the mutex.
 *	2) A jthread dedicated to processing the queue when it has items, and sleeping when it does not
 *	3) A condition variable used to ensure the jthread only runs when it needs to.
 *
 *	LOCK ORDERING
 *	We are using two mutexes that could conflict with each other
 *	1) Mutex for whether the worker is active
 *	2) Mutex for access to the event queue
 *
 *	There is a potential deadlock if the worker gets ownership of his isActive mutex,
 *	while another thread owns the event queue mutex. This is because before the worker decides to "clock out",
 *	they would check if they have work in the queue. Since the queue is owned, deadlock.
 *	*****ALWAYS LOCK THE IS_WORKER_ACTIVE MUTEX BEFORE THE QUEUE MUTEX*******
 *
 *	The inverse is possible if the code is changed so that: the worker owns the queue the entire time its doing work,
 *	until it's empty. At the time of this warning, this is not the case.
 */
class MessageEventManager {
	public:
		explicit MessageEventManager(std::function<void(WorkerEvent*)> sendUpdateFunction);
		virtual ~MessageEventManager();
		void enqueue(JobEvent::Status status, double progress, double timepoint, const char *eventMessage);
		void enqueue(JobEvent::Status status, double progress, double timepoint);
		void enqueue(JobEvent::Status status, const char *eventMessage);
		void requestStopAndWaitForIt();
		bool stopWasCalled();

	private:
		void performQueueProcessing();
		void processQueue();
		void processEvent(WorkerEvent* event);
		void enqueue(WorkerEvent*);

		std::thread eventQueueProcessingWorkerThread; //TODO: make a `std::jthread` once compilers catch up with standard
		std::function<void(WorkerEvent*)> sendUpdateFunction;

		// Critical Resources / Regions
		// -> CR #1 - Whether stop has been requested or not
		bool stopRequested;
		std::condition_variable requestedStopForeman;
		std::mutex stopRequestedMutex;
		// -> CR #2 - The Event Queue
		std::queue<WorkerEvent*> eventQueue;
		std::condition_variable eventQueueForeman;
		std::mutex queuetex;
};

#endif //VCELL_ODE_NUMERICS_MESSAGEEVENTQUEUE_H