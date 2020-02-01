#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <wchan.h>
#include <opt-A1.h>
#include <array.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */

// static struct semaphore *intersectionSem;
static struct cv *cvSouth;
static struct cv *cvNorth;
static struct cv *cvEast;
static struct cv *cvWest;
static struct lock *lock;

struct Car {
  Direction origin;
  Direction destination;
} Car;

static volatile int entered = 0;
static volatile Direction direction;

static volatile int cars_waiting[] = {0,0,0,0}; 


/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */

void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */
  cvSouth = cv_create("cv south");
  cvNorth = cv_create("cv north");
  cvEast = cv_create("cv east");
  cvWest = cv_create("cv");
  lock = lock_create("intersectionLock");

  if (cvSouth == NULL) {
    panic("could not create CV");
  }
  if (cvNorth == NULL) {
    panic("could not create CV");
  }
  if (cvEast == NULL) {
    panic("could not create CV");
  }
  if (cvWest == NULL) {
    panic("could not create CV");
  }

  if (lock == NULL) {
    panic("Could not create lock :(");
  }

  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */

  KASSERT(cvNorth != NULL);
  cv_destroy(cvNorth);
  KASSERT(cvSouth != NULL);
  cv_destroy(cvSouth);
  KASSERT(cvEast != NULL);
  cv_destroy(cvEast);
  KASSERT(cvWest != NULL);
  cv_destroy(cvWest);

  KASSERT(lock != NULL);
  lock_destroy(lock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  lock_acquire(lock);

  if (entered == 0 && cars_waiting[north] == 0 && cars_waiting[south] == 0 && cars_waiting[east] == 0 && cars_waiting[west] == 0) {
    cars_waiting[origin] ++;
    direction = origin; // car can go right away
  } else if (origin == direction) { // car can go right away if it was waiting
  cars_waiting[origin] ++;
  } else {
    cars_waiting[origin] ++;
    if (origin == north) {
      cv_wait(cvNorth, lock);
    }
    else if (origin == south) {
      cv_wait(cvSouth, lock);
    }
    else if (origin == east) {
      cv_wait(cvEast, lock);
    }
    else if (origin == west) {
      cv_wait(cvWest, lock);
    }
  }
  entered ++;

  (void)destination;
  
  cars_waiting[origin] --;
  lock_release(lock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  lock_acquire(lock);

  entered --;

  // cars_waiting[origin] --;

  if (entered == 0) {

    struct cv *next = cvEast;
    if (direction == south) next = cvSouth;
    if (direction == north) next = cvNorth;
    if (direction == west) next = cvWest;

    // when no cars are in the intersection, do a swappy swap to send new cars off
    if (direction == north) {
      if (cars_waiting[east] > 0) {next = cvEast; direction = east;}
      else if (cars_waiting[south] > 0) {next = cvSouth; direction = south;}
      else if (cars_waiting[west] > 0) {next = cvWest; direction = west;}
    } else if (direction == east) {
      if (cars_waiting[south] > 0) {next = cvSouth; direction = south;}
      else if (cars_waiting[west] > 0) {next = cvWest; direction = west;}
      else if (cars_waiting[north] > 0) {next = cvNorth; direction = north;}
    } else if (direction == south) {
      if (cars_waiting[west] > 0) {next = cvWest; direction = west;}
      else if (cars_waiting[north] > 0) {next = cvNorth; direction = north;}
      else if (cars_waiting[east] > 0) {next = cvEast; direction = east;}
    } else if (direction == west) {
      if (cars_waiting[north] > 0) {next = cvNorth; direction = north;}
      else if (cars_waiting[east] > 0) {next = cvEast; direction = east;}
      else if (cars_waiting[south] > 0) {next = cvSouth; direction = south;}
    }

    // if (cars_waiting[east] > 0) {next = cvEast; direction = east;}
    // else if (cars_waiting[south] > 0) {next = cvSouth; direction = south;}
    // else if (cars_waiting[west] > 0) {next = cvWest; direction = west;}
    // else if (cars_waiting[north] > 0) {next = cvNorth; direction = north;}

    cv_broadcast(next, lock);
  }

  (void)origin;
  (void)destination;
  lock_release(lock);
}
