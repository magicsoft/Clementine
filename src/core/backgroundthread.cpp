/* This file is part of Clementine.

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "backgroundthread.h"

int BackgroundThreadBase::CreateInThreadEvent::sEventType = -1;

BackgroundThreadBase::BackgroundThreadBase(QObject *parent)
  : QThread(parent),
    io_priority_(IOPRIO_CLASS_NONE),
    cpu_priority_(InheritPriority),
    object_creator_(NULL)
{
  if (CreateInThreadEvent::sEventType == -1)
    CreateInThreadEvent::sEventType = QEvent::registerEventType();
}

BackgroundThreadBase::CreateInThreadEvent::CreateInThreadEvent(CreateInThreadRequest *req)
  : QEvent(QEvent::Type(sEventType)),
    req_(req)
{
}

bool BackgroundThreadBase::ObjectCreator::event(QEvent* e) {
  if (e->type() != CreateInThreadEvent::sEventType)
    return false;

  // Create the object, parented to this object so it gets destroyed when the
  // thread ends.
  CreateInThreadRequest* req = static_cast<CreateInThreadEvent*>(e)->req_;
  req->object_ = req->meta_object_.newInstance(Q_ARG(QObject*, this));

  // Wake up the calling thread
  QMutexLocker l(&req->mutex_);
  req->wait_condition_.wakeAll();

  return true;
}


int BackgroundThreadBase::SetIOPriority() {
#ifdef Q_OS_LINUX
  return syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, gettid(),
                 4 | io_priority_ << IOPRIO_CLASS_SHIFT);
#elif defined(Q_OS_DARWIN)
  return setpriority(PRIO_DARWIN_THREAD, 0,
                     io_priority_ == IOPRIO_CLASS_IDLE ? PRIO_DARWIN_BG : 0);
#else
  return 0;
#endif
}

int BackgroundThreadBase::gettid() {
#ifdef Q_OS_LINUX
  return syscall(SYS_gettid);
#else
  return 0;
#endif
}

void BackgroundThreadBase::Start(bool block) {
  if (!block) {
    // Just start the thread and return immediately
    start(cpu_priority_);
    return;
  }

  // Lock the mutex so the new thread won't try to wake us up before we start
  // waiting.
  QMutexLocker l(&started_wait_condition_mutex_);

  // Start the thread.
  start(cpu_priority_);

  // Wait for the thread to initalise.
  started_wait_condition_.wait(l.mutex());
}

