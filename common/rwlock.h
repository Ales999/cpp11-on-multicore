//---------------------------------------------------------
// For conditions of distribution and use, see
// https://github.com/preshing/cpp11-on-multicore/blob/master/LICENSE
//---------------------------------------------------------

#ifndef __CPP11OM_RWLOCK_H__
#define __CPP11OM_RWLOCK_H__

#include <cassert>
#include <atomic>
#include <random>
#include "sema.h"
#include "bitfield.h"


//---------------------------------------------------------
// NonRecursiveRWLock
//---------------------------------------------------------
class NonRecursiveRWLock
{
private:
    BEGIN_BITFIELD_TYPE(Status, uint32_t)
        ADD_BITFIELD_MEMBER(readers, 0, 8)
        ADD_BITFIELD_MEMBER(waitToRead, 8, 8)
        ADD_BITFIELD_MEMBER(writers, 16, 8)
    END_BITFIELD_TYPE()

    std::atomic<uint32_t> m_status;
    DefaultSemaphoreType m_readSema;
    DefaultSemaphoreType m_writeSema;

public:
    NonRecursiveRWLock() : m_status(0) {}
    
    void lockReader()
    {
        Status oldStatus = m_status.load(std::memory_order_relaxed);
        Status newStatus;
        do
        {
            newStatus = oldStatus;
            if (oldStatus.writers().get() > 0)
            {
                newStatus.waitToRead().add(1);
            }
            else
            {
                newStatus.readers().add(1);
            }
        }
        while (!m_status.compare_exchange_weak(oldStatus.value, newStatus.value,
                                               std::memory_order_acquire, std::memory_order_relaxed));

        if (oldStatus.writers().get() > 0)
        {
            m_readSema.wait();
        }
    }

    void unlockReader()
    {
        Status oldStatus = m_status.fetch_sub(Status().readers().one(), std::memory_order_release);
        assert(oldStatus.readers().get() > 0);
        if (oldStatus.readers().get() == 1 && oldStatus.writers().get() > 0)
        {
            m_writeSema.signal();
        }
    }

    void lockWriter()
    {
        Status oldStatus = m_status.fetch_add(Status().writers().one(), std::memory_order_acquire);
        assert(oldStatus.writers().get() + 1 <= Status().writers().maximum());
        if (oldStatus.readers().get() > 0 || oldStatus.writers().get() > 0)
        {
            m_writeSema.wait();
        }
    }

    void unlockWriter()
    {
        Status oldStatus = m_status.load(std::memory_order_relaxed);
        Status newStatus;
        uint32_t waitToRead = 0;
        do
        {
            assert(oldStatus.readers().get() == 0);
            newStatus = oldStatus;
            newStatus.writers().sub(1);
            waitToRead = oldStatus.waitToRead().get();
            if (waitToRead > 0)
            {
                newStatus.waitToRead().set(0);
                newStatus.readers().set(waitToRead);
            }
        }
        while (!m_status.compare_exchange_weak(oldStatus.value, newStatus.value,
                                               std::memory_order_release, std::memory_order_relaxed));

        if (waitToRead > 0)
        {
            m_readSema.signal(waitToRead);
        }
        else if (oldStatus.writers().get() > 1)
        {
            m_writeSema.signal();
        }
    }
};


//---------------------------------------------------------
// ReadLockGuard
//---------------------------------------------------------
template <class LockType>
class ReadLockGuard
{
private:
    LockType& m_lock;

public:
    ReadLockGuard(LockType& lock) : m_lock(lock)
    {
        m_lock.lockReader();
    }

    ~ReadLockGuard()
    {
        m_lock.unlockReader();
    }
};


//---------------------------------------------------------
// WriteLockGuard
//---------------------------------------------------------
template <class LockType>
class WriteLockGuard
{
private:
    LockType& m_lock;

public:
    WriteLockGuard(LockType& lock) : m_lock(lock)
    {
        m_lock.lockWriter();
    }

    ~WriteLockGuard()
    {
        m_lock.unlockWriter();
    }
};


#endif // __CPP11OM_RWLOCK_H__
