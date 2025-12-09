//
// Created by Logan Drescher on 11/24/25.
//
#include <VCELL/MessageEventManager.h>
#include <iostream>
#include <utility>

MessageEventManager::MessageEventManager(std::function<void(WorkerEvent*)> sendUpdateFunction):
		stopRequested{false},
		sendUpdateFunction{std::move(sendUpdateFunction)},
		eventQueueProcessingWorkerThread([this]() {this->processQueue();}) // Should auto-start thread
{

}

MessageEventManager::~MessageEventManager() {
	if (!this->stopRequested) this->requestStopAndWaitForIt();
}

void MessageEventManager::enqueue(const JobEvent::Status status, const double progress, const double timepoint, const char *eventMessage) {
	this->enqueue(new WorkerEvent(status, progress, timepoint, eventMessage));
}

void MessageEventManager::enqueue(const JobEvent::Status status, const double progress, const double timepoint) {
	this->enqueue(status, progress, timepoint, "");
}

void MessageEventManager::enqueue(const JobEvent::Status status, const char *eventMessage) {
	this->enqueue(status, 0, 0, eventMessage);
}

void MessageEventManager::requestStopAndWaitForIt() {
	std::unique_lock stopRequestedLock{this->stopRequestedMutex};
	this->stopRequested = true;
	this->needMessageProcessingForeman.notify_all();
	this->requestedStopForeman.wait(stopRequestedLock, [this]()->bool{return this->eventQueue.empty();});
}

bool MessageEventManager::stopWasCalled() {
	std::unique_lock stopRequestedLock{this->stopRequestedMutex};
	return this->stopRequested;

}

///////////////////////////////////////////////////
///				Private Methods					///
///////////////////////////////////////////////////

void MessageEventManager::processQueue() {
	while (true) {
		WorkerEvent* event;
		{// START Clock-out Scope //
			// The order of the locks is important! See Header File
			std::unique_lock checkIfTheresWorkLock{this->timeClockMutex}; // This is to prevent loop processing while `enqueue` is being called
			std::unique_lock shouldBeActiveLock(this->queuetex);
			if (this->eventQueue.empty()) {
				{ // START stop-requested Scope
					std::unique_lock stopRequestedLock{this->stopRequestedMutex};
					if (this->stopRequested) {
						this->requestedStopForeman.notify_all();
						return; // We're all done; since this should be done on a jthread, this should also trigger a join.
					}
				} // END stop-requested Scope
				checkIfTheresWorkLock.unlock(); // Need to allow enqueuing, can't do that with this locked, and we can't rescope but unique_lock order matters above!
				// The worker can stop working, and "get some sleep"
				// Wait for the worker to be "prodded" (via `needMessagingForeman.notify_one()`), AND the event queue is not empty
				// Note that this `wait()` call, by design, unlocks the queue mutex, until it is "prodded".
				this->needMessageProcessingForeman.wait(shouldBeActiveLock);
				if (this->eventQueue.empty()) continue; // Probably means we need to check if stop was requested again
			}
			event = this->eventQueue.front();
			this->eventQueue.pop();
		}// END Clock-out Scope  //

		// Process Event
		this->processEvent(event);
		// Remember to delete the event! We need the memory back!
		delete event;
	}
}

void MessageEventManager::processEvent(WorkerEvent* event) {
	this->sendUpdateFunction(event);
}

void MessageEventManager::enqueue(WorkerEvent* event) {
	// The order of the locks is important! See Header File
	std::lock_guard workerIsActiveLock{this->timeClockMutex}; // Need to lock to prevent worker from "clocking out" while we set this up
	std::lock_guard stopRequestedLock{this->stopRequestedMutex}; // We scope this lock to the whole function; "last in the door" policy
	if (this->stopRequested) {
		std::cerr << "A new event was added to the messaging queue, but this queue has had `stop` requested!" << std::endl;
		delete event;
		return; // note: on this return function scope ends, and thus so too does out function-scoped locks
	}

	{// START Emplace Scope //
		std::lock_guard queueLock{this->queuetex};
		this->eventQueue.emplace(event);
	}// END Emplace Scope  //

	this->needMessageProcessingForeman.notify_one(); // Nudges one sleeping worker awake. If the worker is awake...this does nothing; as it should.
}