#include "file_mutex.h"

#ifdef POSIX

namespace gb::yadro::util
{
    //-------------------------------------------------------------------------
    file_mutex::file_mutex(const char* file_name)
    {
        _handle = ::open(file_name, O_RDWR);
        if (_handle == invalid_handle)
            _handle = ::open(file_name, O_CREAT | O_RDWR, 0666);

        gbassert(_handle != invalid_handle);
    }

    //-------------------------------------------------------------------------
    file_mutex::~file_mutex()
    {
        if (_handle != invalid_handle)
        {
            ::close(_handle);
        }
    }

    //-------------------------------------------------------------------------
    void file_mutex::lock()
    {
        // acquire file lock
        struct flock lock;
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;

        gbassert(-1 != ::fcntl(_handle, F_SETLKW, &lock));
    }

    //-------------------------------------------------------------------------
    bool file_mutex::try_lock()
    {
        struct flock lock;
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;
        auto ret = ::fcntl(_handle, F_SETLK, &lock);

        gbassert(ret != -1 || errno == EAGAIN || errno == EACCES);

        return ret != -1;
    }

    //-------------------------------------------------------------------------
    void file_mutex::unlock()
    {
        struct flock lock;
        lock.l_type = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;
        gbassert(-1 != ::fcntl(_handle, F_SETLK, &lock));
    }

    //-------------------------------------------------------------------------
    void file_mutex::lock_shared()
    {
        struct flock lock;
        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;
        gbassert(-1 != ::fcntl(_handle, F_SETLKW, &lock));
    }

    //-------------------------------------------------------------------------
    bool file_mutex::try_lock_shared()
    {
        struct flock lock;
        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;
        auto ret = ::fcntl(_handle, F_SETLK, &lock);
        gbassert(ret != -1 || errno == = EAGAIN || errno == EACCES);
        return ret != -1;
    }

    //-------------------------------------------------------------------------
    void file_mutex::unlock_shared()
    {
        struct flock lock;
        lock.l_type = F_UNLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;
        gbassert(-1 != ::fcntl(_handle, F_SETLK, &lock));
    }
}

#endif