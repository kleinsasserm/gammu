/*
 * python-gammu - Phone communication libary
 * Copyright © 2003 - 2010 Michal Čihař
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/* Python-gammu configuration */
#include <Python.h>

/* Gammu includes */
#include <gammu.h>
#include <gammu-smsd.h>

/* Locales */
#include <locale.h>

/* Strings */
#include "../../../helper/string.h"

/* For locking */
#ifdef WITH_THREAD
#include "pythread.h"
#endif

/* Convertors between Gammu and Python types */
#include "convertors.h"

/* Error objects */
#include "errors.h"

/* Data objects */
#include "data.h"

/* Other useful stuff */
#include "misc.h"

/* Length of buffers used in most of code */
#define BUFFER_LENGTH 255

#ifdef WITH_THREAD

/* Use python locking */

#define BEGIN_PHONE_COMM \
    Py_BEGIN_ALLOW_THREADS \
    PyThread_acquire_lock(self->mutex, 1);

#define END_PHONE_COMM \
    PyThread_release_lock(self->mutex); \
    Py_END_ALLOW_THREADS \
    CheckIncomingEvents(self);

#else

/* No need for locking when no threads */
#define BEGIN_PHONE_COMM
#define END_PHONE_COMM \
    CheckIncomingEvents(self);

#endif

PyObject    *DebugFile;

#define MAX_EVENTS 10

/* ----------------------------------------------------- */

/* Declarations for objects of type StateMachine */
typedef struct {
    PyObject_HEAD

    GSM_StateMachine    *s;
    PyObject            *DebugFile;
    PyObject            *IncomingCallback;
    volatile GSM_Error  SMSStatus;
    volatile int        MessageReference;
    GSM_Call            *IncomingCallQueue[MAX_EVENTS + 1];
    GSM_SMSMessage      *IncomingSMSQueue[MAX_EVENTS + 1];
    GSM_CBMessage       *IncomingCBQueue[MAX_EVENTS + 1];
    GSM_USSDMessage     *IncomingUSSDQueue[MAX_EVENTS + 1];
    GSM_MemoryType      memory_entry_cache_type;
    int                 memory_entry_cache;
    int                 todo_entry_cache;
    int                 calendar_entry_cache;
#ifdef WITH_THREAD
    PyThread_type_lock mutex;
#endif
} StateMachineObject;


static char gammu_Version__doc__[] =
"Version()\n\n"
"Get version information.\n"
"@return: Tuple of version information - Gammu runtime version, python-gammu version, build time Gammu version.\n"
"@rtype: tuple\n"
;

static PyObject *
gammu_Version(PyObject *self)
{
    return Py_BuildValue("s,s,s", GetGammuVersion(), GAMMU_VERSION, GAMMU_VERSION);
}

/* List of methods defined in the module */

static struct PyMethodDef gammu_methods[] = {
    {"Version",         (PyCFunction)gammu_Version,         METH_NOARGS,   gammu_Version__doc__},
    {NULL,	 (PyCFunction)NULL, 0, NULL}		/* sentinel */
};


/* Initialization function for the module (*must* be called initcore) */

static char gammu_module_documentation[] =
"Module wrapping Gammu functions. Gammu is software for communication with GSM phones "
"allowing work with most of data stored in them. Most of functionality is hidden in L{StateMachine} "
"class which does all phone communication.\n\n"
"This documentation describes python-gammu " GAMMU_VERSION ".\n\n"
"This python-gammu has been compiled with Gammu " GAMMU_VERSION ".\n\n"
"@var Errors: Mapping of text representation of errors to gammu error codes. Reverse to L{ErrorNumbers}.\n"
"@var ErrorNumbers: Mapping of gammu error codes to text representation. Reverse to L{Errors}.\n"
;

static struct PyModuleDef gammumodule = {
    PyModuleDef_HEAD_INIT,
    "_gammu",
    gammu_module_documentation,
    -1,
    gammu_methods
};

#ifndef PyMODINIT_FUNC  /* doesn't exists in older python releases */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC PyInit__gammu(void) {
    PyObject *m, *d;
    GSM_Debug_Info *di;

    /* Create the module and add the functions */
    // m = Py_InitModule3("_gammu", gammu_methods, gammu_module_documentation);
    m = PyModule_Create(&gammumodule);

    if (m == NULL)
        return NULL;

    DebugFile = NULL;

    d = PyModule_GetDict(m);

    if (PyType_Ready(&StateMachineType) < 0)
        return NULL;
    Py_INCREF(&StateMachineType);

    //if (PyModule_AddObject(m, "StateMachine", (PyObject *)&StateMachineType) < 0)
    //    return NULL;

    /* SMSD object */
    //if (!gammu_smsd_init(m)) return NULL;

    /* Add some symbolic constants to the module */

    /* Define errors */
    //if (!gammu_create_errors(d)) return NULL;

    /* Define data */
    //if (!gammu_create_data(d)) return NULL;

    /* Check for errors */
    //if (PyErr_Occurred()) {
    //    PyErr_Print();
    //    Py_FatalError("Can not initialize module _gammu, see above for reasons");
    //}

    /* Reset debugging setup */
    //di = GSM_GetGlobalDebug();
    //GSM_SetDebugFileDescriptor(NULL, FALSE, di);
    //GSM_SetDebugLevel("none", di);

    return m;
}
/*
 * vim: expandtab sw=4 ts=4 sts=4:
 */
