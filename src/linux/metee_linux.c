/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2014-2019 Intel Corporation
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <linux/mei.h>
#include "metee.h"
#include "helpers.h"
#include <libmei.h>

/* use inline function instead of macro to avoid -Waddress warning in GCC */
static inline struct mei *to_mei(PTEEHANDLE _h) __attribute__((always_inline));
static inline struct mei *to_mei(PTEEHANDLE _h)
{
	return _h ? (struct mei *)_h->handle : NULL;
}

static inline int __mei_select(struct mei *me, bool on_read, unsigned long timeout)
{
	int rv;
	fd_set rset, wset;
	struct timeval tv;

	tv.tv_sec =  timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000000;

	FD_ZERO(&rset);
	FD_ZERO(&wset);
	if (on_read)
		FD_SET(me->fd, &rset);
	else
		FD_SET(me->fd, &wset);
	errno = 0;
	rv = select(me->fd + 1 , &rset, &wset, NULL, &tv);
	if (rv < 0)
		return -errno;
	if (rv == 0)
		return -ETIME;
	return 0;
}


static inline TEESTATUS errno2status(int err)
{
	switch (err) {
		case 0      : return TEE_SUCCESS;
		case -ENOTTY: return TEE_CLIENT_NOT_FOUND;
		case -EBUSY : return TEE_BUSY;
		case -ENODEV: return TEE_DISCONNECTED;
		case -ETIME : return TEE_TIMEOUT;
		default     : return TEE_INTERNAL_ERROR;
	}
}

TEESTATUS TEEAPI TeeInit(IN OUT PTEEHANDLE handle, IN const GUID *guid, IN OPTIONAL const char *device)
{
	struct mei *me;
	TEESTATUS  status;
#if defined(DEBUG) && !defined(SYSLOG)
	bool verbose = true;
#else
	bool verbose = false;
#endif // DEBUG and !SYSLOG

	FUNC_ENTRY();

	if (guid == NULL || handle == NULL) {
		ERRPRINT("One of the parameters was illegal");
		status = TEE_INVALID_PARAMETER;
		goto End;
	}

	__tee_init_handle(handle);
	me = mei_alloc(device ? device : MEI_DEFAULT_DEVICE, guid, 0, verbose);
	if (!me) {
		ERRPRINT("Cannot init mei structure\n");
		status = TEE_INTERNAL_ERROR;
		goto End;
	}
	handle->handle = me;
	status = TEE_SUCCESS;

End:
	FUNC_EXIT(status);
	return status;
}

TEESTATUS TEEAPI TeeConnect(IN OUT PTEEHANDLE handle)
{
	struct mei *me = to_mei(handle);
	TEESTATUS  status;
	int        rc;


	FUNC_ENTRY();

	if (!me) {
		ERRPRINT("One of the parameters was illegal");
		status = TEE_INVALID_PARAMETER;
		goto End;
	}

	rc = mei_connect(me);
	if (rc) {
		ERRPRINT("Cannot establish a handle to the Intel MEI driver\n");
		status = errno2status(rc);
		goto End;
	}

	handle->maxMsgLen = me->buf_size;
	handle->protcolVer = me->prot_ver;

	status = TEE_SUCCESS;

End:
	FUNC_EXIT(status);
	return status;
}

TEESTATUS TEEAPI TeeRead(IN PTEEHANDLE handle, IN OUT void *buffer, IN size_t bufferSize,
			 OUT OPTIONAL size_t *pNumOfBytesRead, IN OPTIONAL uint32_t timeout)
{
	struct mei *me = to_mei(handle);
	TEESTATUS status;
	ssize_t rc;

	FUNC_ENTRY();

	if (!me || !buffer || !bufferSize) {
		ERRPRINT("One of the parameters was illegal");
		status = TEE_INVALID_PARAMETER;
		goto End;
	}

	DBGPRINT("call read length = %zd\n", bufferSize);

	if (timeout && (rc = __mei_select(me, true, timeout))) {
		status = errno2status(rc);
		ERRPRINT("select failed with status %zd %s\n",
				rc, strerror(rc));
		goto End;
	}

	rc = mei_recv_msg(me, buffer, bufferSize);
	if (rc < 0) {
		status = errno2status(rc);
		ERRPRINT("read failed with status %zd %s\n",
				rc, strerror(rc));
		goto End;
	}

	status = TEE_SUCCESS;
	DBGPRINT("read succeeded with result %zd\n", rc);
	if (pNumOfBytesRead)
		*pNumOfBytesRead = rc;

End:
	FUNC_EXIT(status);
	return status;
}

TEESTATUS TEEAPI TeeWrite(IN PTEEHANDLE handle, IN const void *buffer, IN size_t bufferSize,
			  OUT OPTIONAL size_t *numberOfBytesWritten, IN OPTIONAL uint32_t timeout)
{
	struct mei *me  =  to_mei(handle);
	TEESTATUS status;
	ssize_t rc;

	FUNC_ENTRY();

	if (!me || !buffer || !bufferSize) {
		ERRPRINT("One of the parameters was illegal");
		status = TEE_INVALID_PARAMETER;
		goto End;
	}

	DBGPRINT("call write length = %zd\n", bufferSize);

	if (timeout && (rc = __mei_select(me, false, timeout))) {
		status = errno2status(rc);
		ERRPRINT("select failed with status %zd %s\n",
				rc, strerror(rc));
		goto End;
	}

	rc  = mei_send_msg(me, buffer, bufferSize);
	if (rc < 0) {
		status = errno2status(rc);
		ERRPRINT("write failed with status %zd %s\n", rc, strerror(rc));
		goto End;
	}

	if (numberOfBytesWritten)
		*numberOfBytesWritten = rc;

	status = TEE_SUCCESS;
End:
	FUNC_EXIT(status);
	return status;
}

void TEEAPI TeeDisconnect(PTEEHANDLE handle)
{
	struct mei *me  =  to_mei(handle);
	FUNC_ENTRY();
	if (me) {
		mei_free(me);
		handle->handle = NULL;
	}

	FUNC_EXIT(TEE_SUCCESS);
}

TEE_DEVICE_HANDLE TEEAPI TeeGetDeviceHandle(IN PTEEHANDLE handle)
{
	struct mei *me = to_mei(handle);

	FUNC_ENTRY();

	if (!me) {
		ERRPRINT("One of the parameters was illegal");
		FUNC_EXIT(TEE_INVALID_PARAMETER)
		return TEE_INVALID_DEVICE_HANDLE;
	}
	
	FUNC_EXIT(TEE_SUCCESS);
	return me->fd;
}

TEESTATUS TEEAPI GetDriverVersion(IN PTEEHANDLE handle, IN OUT teeDriverVersion_t *driverVersion)
{
	struct mei *me = to_mei(handle);
	TEESTATUS status;

	FUNC_ENTRY();

	if (!me || !driverVersion) {
		ERRPRINT("One of the parameters was illegal");
		status = TEE_INVALID_PARAMETER;
		goto End;
	}
	
	status = TEE_NOTSUPPORTED;
End:
	FUNC_EXIT(status);
	return status;
}
