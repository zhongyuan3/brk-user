/*
 * errno storage is per-thread.  __errno_location() lives in
 * libbrkc/pthread/pthread_self.c and returns the current TCB's err field.
 */
