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

/* Other useful stuff */
#include "stdio.h"

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
//    CheckIncomingEvents(self);

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

static void 
StateMachine_dealloc(StateMachineObject *self) 
{ 
    BEGIN_PHONE_COMM 
    if (GSM_IsConnected(self->s)) { 
        /* Disable any possible incoming notifications */ 
        GSM_SetIncomingSMS(self->s, FALSE); 
        GSM_SetIncomingCall(self->s, FALSE); 
        GSM_SetIncomingCB(self->s, FALSE); 
        GSM_SetIncomingUSSD(self->s, FALSE); 
        /* Terminate the connection */ 
        GSM_TerminateConnection(self->s); 
    } 
    GSM_FreeStateMachine(self->s); 
    self->s = NULL; 
    END_PHONE_COMM 
 
    if (self->DebugFile != NULL) { 
        Py_DECREF(self->DebugFile); 
        self->DebugFile = NULL; 
    } 
 
#ifdef WITH_THREAD 
    PyThread_free_lock(self->mutex); 
#endif 
    //self->ob_type->tp_free((PyObject*)self); 
    Py_TYPE(self)->tp_free((PyObject*)self); 
} 

static int
StateMachine_init(StateMachineObject *self, PyObject *args, PyObject *kwds)
{
    char                *s = NULL;
    static char         *kwlist[] = {"Locale", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|s", kwlist, &s))
        return 0;

    if (s != NULL && strcmp(s, "auto") == 0) {
        s = NULL;
    }

    /* Reset our structures */
    self->DebugFile         = NULL;
    self->IncomingCallback  = NULL;

    self->IncomingCallQueue[0] = NULL;
    self->IncomingSMSQueue[0] = NULL;
    self->IncomingCBQueue[0] = NULL;
    self->IncomingUSSDQueue[0] = NULL;

    /* Create phone communication lock */
#ifdef WITH_THREAD
    self->mutex = PyThread_allocate_lock();
#endif

    /* Init Gammu locales, we don't care about NULL, it's handled correctly */
    GSM_InitLocales(s);

    return 1;
}

static PyObject *
StateMachine_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    StateMachineObject *self;

    self = (StateMachineObject *)type->tp_alloc(type, 0);
    self->s = GSM_AllocStateMachine();

    return (PyObject *)self;
}

/***********/
/* SendSMS */
/***********/

static char StateMachine_SendSMS__doc__[] =
"SendSMS(Value)\n\n"
"Sends SMS.\n\n"
"@param Value: SMS data\n"
"@type Value: hash\n"
"@return: Message reference as integer\n"
"@rtype: int\n"
;

static PyObject *
StateMachine_SendSMS(StateMachineObject *self, PyObject *args, PyObject *kwds) {
    GSM_Error           error;
    GSM_SMSMessage      sms;
    PyObject            *value;
    static char         *kwlist[] = {"Value", NULL};
    int                 i = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                &PyDict_Type, &(value)))
        return NULL;

    if (!SMSFromPython(value, &sms, 0, 0, 1)) return NULL;

    self->SMSStatus = ERR_TIMEOUT;

    BEGIN_PHONE_COMM
    error = GSM_SendSMS(self->s, &sms);
    END_PHONE_COMM

    if (!checkError(self->s, error, "SendSMS")) return NULL;

    while (self->SMSStatus != ERR_NONE) {
        i++;
        BEGIN_PHONE_COMM
        GSM_ReadDevice(self->s, TRUE);
        END_PHONE_COMM
        if (self->SMSStatus == ERR_FULL || self->SMSStatus == ERR_UNKNOWN || i == 100) {
            if (!checkError(self->s, self->SMSStatus, "SendSMS")) {
                return NULL;
            }
        }
    }

    return PyLong_FromLong(self->MessageReference);
}

static char StateMachine_ReadConfig__doc__[] =
"ReadConfig(Section, Configuration, Filename)\n\n"
"Reads specified section of gammurc\n\n"
"@param Section: Index of config section to read. Defaults to 0.\n"
"@type Section: int\n"
"@param Configuration: Index where config section will be stored. Defaults to Section.\n"
"@type Configuration: int\n"
"@param Filename: Path to configuration file (otherwise it is autodetected).\n"
"@type Filename: string\n"
"@return: None\n"
"@rtype: None\n"
;

static PyObject *
StateMachine_ReadConfig(StateMachineObject *self, PyObject *args, PyObject *kwds)
{
    GSM_Error       error;
    int             section = 0;
    int             dst = -1;
    INI_Section     *cfg;
    char            *cfg_path = NULL;
    GSM_Config *Config;

    static char         *kwlist[] = {"Section", "Configuration", "Filename", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|IIs", kwlist, &section, &dst, &cfg_path))
        return NULL;

    if (dst == -1) dst = section;
    Config = GSM_GetConfig(self->s, dst);
    if (Config == NULL) {
        PyErr_Format(PyExc_ValueError, "Maximal configuration storage exceeded");
        return NULL;
    }

    error = GSM_FindGammuRC(&cfg, cfg_path);
    if (!checkError(self->s, error, "FindGammuRC via ReadConfig")) {
        PyErr_SetString(PyExc_IOError, "gammurc configuration file not found");
        return NULL;
    }

    if (cfg == NULL) {
        PyErr_SetString(PyExc_IOError, "Can not read gammurc");
        return NULL;
    }

    error = GSM_ReadConfig(cfg, Config, section);
    if (!checkError(self->s, error, "ReadConfig")) {
        INI_Free(cfg);
        return NULL;
    }
    Config->UseGlobalDebugFile = FALSE;
    

    /* Tell Gammu we have configured another section */
    GSM_SetConfigNum(self->s, dst + 1);

    INI_Free(cfg);

    Py_RETURN_NONE;
}

static char StateMachine_Init__doc__[] =
"Init(Replies)\n\n"
"Initialises the connection with phone.\n\n"
"@param Replies: Number of replies to wait for on each request. Defaults to 1.\n"
"@type Replies: int\n"
"@return: None\n"
"@rtype: None\n"
;

static PyObject *
StateMachine_Init(StateMachineObject *self, PyObject *args, PyObject *kwds)
{
    GSM_Error           error;
    int                 replies = 1;
    static char         *kwlist[] = {"Replies", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|I", kwlist, &replies))
        return NULL;

    BEGIN_PHONE_COMM
    error = GSM_InitConnection(self->s, replies);
    END_PHONE_COMM
    if (!checkError(self->s, error, "Init"))
        return NULL;

    /* Set callbacks */
    GSM_SetIncomingCallCallback(self->s, IncomingCall, self);
    GSM_SetIncomingSMSCallback(self->s, IncomingSMS, self);
    GSM_SetIncomingCBCallback(self->s, IncomingCB, self);
    GSM_SetIncomingUSSDCallback(self->s, IncomingUSSD, self);
    GSM_SetSendSMSStatusCallback(self->s, SendSMSStatus, self);

    /* No cached data */
    self->memory_entry_cache_type = 0;
    self->memory_entry_cache = 1;
    self->todo_entry_cache = 1;
    self->calendar_entry_cache = 1;

    Py_RETURN_NONE;
}

static char StateMachineType__doc__[] =
"StateMachine(Locale)\n\n"
"StateMachine object, that is used for communication with phone.\n\n"
/* FIXME: following doc should go elsewhere */
"param Locale: What locales to use for gammu error messages, default is 'auto' which does autodetection according to user locales\n"
"type Locale: string\n"
;

static struct PyMethodDef StateMachine_methods[] = {
    {"ReadConfig",	(PyCFunction)StateMachine_ReadConfig,	METH_VARARGS|METH_KEYWORDS,	StateMachine_ReadConfig__doc__},
//    {"SetConfig",	(PyCFunction)StateMachine_SetConfig,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetConfig__doc__},
//    {"GetConfig",	(PyCFunction)StateMachine_GetConfig,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetConfig__doc__},
    {"Init",	(PyCFunction)StateMachine_Init,	METH_VARARGS|METH_KEYWORDS,	StateMachine_Init__doc__},
//    {"Terminate",	(PyCFunction)StateMachine_Terminate,	METH_VARARGS|METH_KEYWORDS,	StateMachine_Terminate__doc__},
//    {"Abort",	(PyCFunction)StateMachine_Abort,	METH_VARARGS|METH_KEYWORDS,	StateMachine_Abort__doc__},
//    {"ReadDevice",	(PyCFunction)StateMachine_ReadDevice,	METH_VARARGS|METH_KEYWORDS,	StateMachine_ReadDevice__doc__},
//    {"GetManufacturer",	(PyCFunction)StateMachine_GetManufacturer,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetManufacturer__doc__},
//    {"GetModel",	(PyCFunction)StateMachine_GetModel,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetModel__doc__},
//    {"GetFirmware",	(PyCFunction)StateMachine_GetFirmware,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetFirmware__doc__},
//    {"GetIMEI",	(PyCFunction)StateMachine_GetIMEI,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetIMEI__doc__},
//    {"GetOriginalIMEI",	(PyCFunction)StateMachine_GetOriginalIMEI,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetOriginalIMEI__doc__},
//    {"GetManufactureMonth",	(PyCFunction)StateMachine_GetManufactureMonth,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetManufactureMonth__doc__},
//    {"GetProductCode",	(PyCFunction)StateMachine_GetProductCode,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetProductCode__doc__},
//    {"GetHardware",	(PyCFunction)StateMachine_GetHardware,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetHardware__doc__},
//    {"GetPPM",	(PyCFunction)StateMachine_GetPPM,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetPPM__doc__},
//    {"GetSIMIMSI",	(PyCFunction)StateMachine_GetSIMIMSI,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSIMIMSI__doc__},
//    {"GetDateTime",	(PyCFunction)StateMachine_GetDateTime,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetDateTime__doc__},
//    {"SetDateTime",	(PyCFunction)StateMachine_SetDateTime,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetDateTime__doc__},
//    {"GetAlarm",	(PyCFunction)StateMachine_GetAlarm,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetAlarm__doc__},
//    {"SetAlarm",	(PyCFunction)StateMachine_SetAlarm,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetAlarm__doc__},
//    {"GetLocale",	(PyCFunction)StateMachine_GetLocale,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetLocale__doc__},
//    {"SetLocale",	(PyCFunction)StateMachine_SetLocale,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetLocale__doc__},
//    {"PressKey",	(PyCFunction)StateMachine_PressKey,	METH_VARARGS|METH_KEYWORDS,	StateMachine_PressKey__doc__},
//    {"Reset",	(PyCFunction)StateMachine_Reset,	METH_VARARGS|METH_KEYWORDS,	StateMachine_Reset__doc__},
//    {"ResetPhoneSettings",	(PyCFunction)StateMachine_ResetPhoneSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_ResetPhoneSettings__doc__},
//    {"EnterSecurityCode",	(PyCFunction)StateMachine_EnterSecurityCode,	METH_VARARGS|METH_KEYWORDS,	StateMachine_EnterSecurityCode__doc__},
//    {"GetSecurityStatus",	(PyCFunction)StateMachine_GetSecurityStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSecurityStatus__doc__},
//    {"GetDisplayStatus",	(PyCFunction)StateMachine_GetDisplayStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetDisplayStatus__doc__},
//    {"SetAutoNetworkLogin",	(PyCFunction)StateMachine_SetAutoNetworkLogin,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetAutoNetworkLogin__doc__},
//    {"GetBatteryCharge",	(PyCFunction)StateMachine_GetBatteryCharge,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetBatteryCharge__doc__},
//    {"GetSignalQuality",	(PyCFunction)StateMachine_GetSignalQuality,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSignalQuality__doc__},
//    {"GetNetworkInfo",	(PyCFunction)StateMachine_GetNetworkInfo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNetworkInfo__doc__},
//    {"GetCategory",	(PyCFunction)StateMachine_GetCategory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetCategory__doc__},
//    {"AddCategory",	(PyCFunction)StateMachine_AddCategory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddCategory__doc__},
//    {"GetCategoryStatus",	(PyCFunction)StateMachine_GetCategoryStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetCategoryStatus__doc__},
//    {"GetMemoryStatus",	(PyCFunction)StateMachine_GetMemoryStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetMemoryStatus__doc__},
//    {"GetMemory",	(PyCFunction)StateMachine_GetMemory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetMemory__doc__},
//    {"GetNextMemory",	(PyCFunction)StateMachine_GetNextMemory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNextMemory__doc__},
//    {"SetMemory",	(PyCFunction)StateMachine_SetMemory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetMemory__doc__},
//    {"AddMemory",	(PyCFunction)StateMachine_AddMemory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddMemory__doc__},
//    {"DeleteMemory",	(PyCFunction)StateMachine_DeleteMemory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteMemory__doc__},
//    {"DeleteAllMemory",	(PyCFunction)StateMachine_DeleteAllMemory,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteAllMemory__doc__},
//    {"GetSpeedDial",	(PyCFunction)StateMachine_GetSpeedDial,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSpeedDial__doc__},
//    {"SetSpeedDial",	(PyCFunction)StateMachine_SetSpeedDial,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetSpeedDial__doc__},
//    {"GetSMSC",	(PyCFunction)StateMachine_GetSMSC,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSMSC__doc__},
//    {"SetSMSC",	(PyCFunction)StateMachine_SetSMSC,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetSMSC__doc__},
//    {"GetSMSStatus",	(PyCFunction)StateMachine_GetSMSStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSMSStatus__doc__},
//    {"GetSMS",	(PyCFunction)StateMachine_GetSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSMS__doc__},
//    {"GetNextSMS",	(PyCFunction)StateMachine_GetNextSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNextSMS__doc__},
//    {"SetSMS",	(PyCFunction)StateMachine_SetSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetSMS__doc__},
//    {"AddSMS",	(PyCFunction)StateMachine_AddSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddSMS__doc__},
//    {"DeleteSMS",	(PyCFunction)StateMachine_DeleteSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteSMS__doc__},
    {"SendSMS",	(PyCFunction)StateMachine_SendSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SendSMS__doc__},
//    {"SendSavedSMS",	(PyCFunction)StateMachine_SendSavedSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SendSavedSMS__doc__},
//    {"SetIncomingSMS",	(PyCFunction)StateMachine_SetIncomingSMS,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetIncomingSMS__doc__},
//    {"SetIncomingCB",	(PyCFunction)StateMachine_SetIncomingCB,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetIncomingCB__doc__},
//    {"SetIncomingCall",	(PyCFunction)StateMachine_SetIncomingCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetIncomingCall__doc__},
//    {"SetIncomingUSSD",	(PyCFunction)StateMachine_SetIncomingUSSD,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetIncomingUSSD__doc__},
//    {"GetSMSFolders",	(PyCFunction)StateMachine_GetSMSFolders,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetSMSFolders__doc__},
//    {"AddSMSFolder",	(PyCFunction)StateMachine_AddSMSFolder,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddSMSFolder__doc__},
//    {"DeleteSMSFolder",	(PyCFunction)StateMachine_DeleteSMSFolder,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteSMSFolder__doc__},
//    {"DialVoice",	(PyCFunction)StateMachine_DialVoice,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DialVoice__doc__},
//    {"DialService",	(PyCFunction)StateMachine_DialService,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DialService__doc__},
//    {"AnswerCall",	(PyCFunction)StateMachine_AnswerCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AnswerCall__doc__},
//    {"CancelCall",	(PyCFunction)StateMachine_CancelCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_CancelCall__doc__},
//    {"HoldCall",	(PyCFunction)StateMachine_HoldCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_HoldCall__doc__},
//    {"UnholdCall",	(PyCFunction)StateMachine_UnholdCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_UnholdCall__doc__},
//    {"ConferenceCall",	(PyCFunction)StateMachine_ConferenceCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_ConferenceCall__doc__},
//    {"SplitCall",	(PyCFunction)StateMachine_SplitCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SplitCall__doc__},
//    {"TransferCall",	(PyCFunction)StateMachine_TransferCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_TransferCall__doc__},
//    {"SwitchCall",	(PyCFunction)StateMachine_SwitchCall,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SwitchCall__doc__},
//    {"SendDTMF",	(PyCFunction)StateMachine_SendDTMF,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SendDTMF__doc__},
//    {"GetCallDivert",	(PyCFunction)StateMachine_GetCallDivert,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetCallDivert__doc__},
//    {"SetCallDivert",	(PyCFunction)StateMachine_SetCallDivert,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetCallDivert__doc__},
//    {"CancelAllDiverts",	(PyCFunction)StateMachine_CancelAllDiverts,	METH_VARARGS|METH_KEYWORDS,	StateMachine_CancelAllDiverts__doc__},
//#if 0
//    {"GetRingtone",	(PyCFunction)StateMachine_GetRingtone,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetRingtone__doc__},
//    {"SetRingtone",	(PyCFunction)StateMachine_SetRingtone,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetRingtone__doc__},
//    {"GetRingtonesInfo",	(PyCFunction)StateMachine_GetRingtonesInfo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetRingtonesInfo__doc__},
//    {"DeleteUserRingtones",	(PyCFunction)StateMachine_DeleteUserRingtones,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteUserRingtones__doc__},
//    {"PlayTone",	(PyCFunction)StateMachine_PlayTone,	METH_VARARGS|METH_KEYWORDS,	StateMachine_PlayTone__doc__},
//    {"GetWAPBookmark",	(PyCFunction)StateMachine_GetWAPBookmark,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetWAPBookmark__doc__},
//    {"SetWAPBookmark",	(PyCFunction)StateMachine_SetWAPBookmark,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetWAPBookmark__doc__},
//    {"DeleteWAPBookmark",	(PyCFunction)StateMachine_DeleteWAPBookmark,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteWAPBookmark__doc__},
//    {"GetWAPSettings",	(PyCFunction)StateMachine_GetWAPSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetWAPSettings__doc__},
//    {"SetWAPSettings",	(PyCFunction)StateMachine_SetWAPSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetWAPSettings__doc__},
//    {"GetMMSSettings",	(PyCFunction)StateMachine_GetMMSSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetMMSSettings__doc__},
//    {"SetMMSSettings",	(PyCFunction)StateMachine_SetMMSSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetMMSSettings__doc__},
//    {"GetBitmap",	(PyCFunction)StateMachine_GetBitmap,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetBitmap__doc__},
//    {"SetBitmap",	(PyCFunction)StateMachine_SetBitmap,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetBitmap__doc__},
//#endif
//    {"GetToDoStatus",	(PyCFunction)StateMachine_GetToDoStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetToDoStatus__doc__},
//    {"GetToDo",	(PyCFunction)StateMachine_GetToDo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetToDo__doc__},
//    {"GetNextToDo",	(PyCFunction)StateMachine_GetNextToDo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNextToDo__doc__},
//    {"SetToDo",	(PyCFunction)StateMachine_SetToDo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetToDo__doc__},
//    {"AddToDo",	(PyCFunction)StateMachine_AddToDo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddToDo__doc__},
//    {"DeleteToDo",	(PyCFunction)StateMachine_DeleteToDo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteToDo__doc__},
//    {"DeleteAllToDo",	(PyCFunction)StateMachine_DeleteAllToDo,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteAllToDo__doc__},
//    {"GetCalendarStatus",	(PyCFunction)StateMachine_GetCalendarStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetCalendarStatus__doc__},
//    {"GetCalendar",	(PyCFunction)StateMachine_GetCalendar,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetCalendar__doc__},
//    {"GetNextCalendar",	(PyCFunction)StateMachine_GetNextCalendar,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNextCalendar__doc__},
//    {"SetCalendar",	(PyCFunction)StateMachine_SetCalendar,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetCalendar__doc__},
//    {"AddCalendar",	(PyCFunction)StateMachine_AddCalendar,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddCalendar__doc__},
//    {"DeleteCalendar",	(PyCFunction)StateMachine_DeleteCalendar,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteCalendar__doc__},
//    {"DeleteAllCalendar",	(PyCFunction)StateMachine_DeleteAllCalendar,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteAllCalendar__doc__},
//#if 0
//    {"GetCalendarSettings",	(PyCFunction)StateMachine_GetCalendarSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetCalendarSettings__doc__},
//    {"SetCalendarSettings",	(PyCFunction)StateMachine_SetCalendarSettings,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetCalendarSettings__doc__},
//    {"GetNote",	(PyCFunction)StateMachine_GetNote,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNote__doc__},
//    {"GetProfile",	(PyCFunction)StateMachine_GetProfile,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetProfile__doc__},
//    {"SetProfile",	(PyCFunction)StateMachine_SetProfile,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetProfile__doc__},
//    {"GetFMStation",	(PyCFunction)StateMachine_GetFMStation,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetFMStation__doc__},
//    {"SetFMStation",	(PyCFunction)StateMachine_SetFMStation,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetFMStation__doc__},
//    {"ClearFMStations",	(PyCFunction)StateMachine_ClearFMStations,	METH_VARARGS|METH_KEYWORDS,	StateMachine_ClearFMStations__doc__},
//#endif
//    {"GetNextFileFolder",	(PyCFunction)StateMachine_GetNextFileFolder,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNextFileFolder__doc__},
//    {"GetFolderListing",	(PyCFunction)StateMachine_GetFolderListing,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetFolderListing__doc__},
//    {"GetNextRootFolder",	(PyCFunction)StateMachine_GetNextRootFolder,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetNextRootFolder__doc__},
//    {"SetFileAttributes",	(PyCFunction)StateMachine_SetFileAttributes,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetFileAttributes__doc__},
//    {"GetFilePart",	(PyCFunction)StateMachine_GetFilePart,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetFilePart__doc__},
//    {"AddFilePart",	(PyCFunction)StateMachine_AddFilePart,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddFilePart__doc__},
//    {"SendFilePart",	(PyCFunction)StateMachine_SendFilePart,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SendFilePart__doc__},
//    {"GetFileSystemStatus",	(PyCFunction)StateMachine_GetFileSystemStatus,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetFileSystemStatus__doc__},
//    {"DeleteFile",	(PyCFunction)StateMachine_DeleteFile,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteFile__doc__},
//    {"AddFolder",	(PyCFunction)StateMachine_AddFolder,	METH_VARARGS|METH_KEYWORDS,	StateMachine_AddFolder__doc__},
//    {"DeleteFolder",	(PyCFunction)StateMachine_DeleteFolder,	METH_VARARGS|METH_KEYWORDS,	StateMachine_DeleteFolder__doc__},
//#if 0
//    {"GetGPRSAccessPoint",	(PyCFunction)StateMachine_GetGPRSAccessPoint,	METH_VARARGS|METH_KEYWORDS,	StateMachine_GetGPRSAccessPoint__doc__},
//    {"SetGPRSAccessPoint",	(PyCFunction)StateMachine_SetGPRSAccessPoint,	METH_VARARGS|METH_KEYWORDS,	StateMachine_SetGPRSAccessPoint__doc__},
//#endif
//    {"SetDebugFile",    (PyCFunction)StateMachine_SetDebugFile,    METH_VARARGS|METH_KEYWORDS,   StateMachine_SetDebugFile__doc__},
//    {"SetDebugLevel",   (PyCFunction)StateMachine_SetDebugLevel,   METH_VARARGS|METH_KEYWORDS,   StateMachine_SetDebugLevel__doc__},
//
//    {"SetIncomingCallback",   (PyCFunction)StateMachine_SetIncomingCallback,   METH_VARARGS|METH_KEYWORDS,   StateMachine_SetIncomingCallback__doc__},
//
    {NULL,		NULL, 0, NULL}		/* sentinel */
};

static PyTypeObject StateMachineType = {
    // PyObject_HEAD_INIT(NULL)
    PyVarObject_HEAD_INIT(NULL, 0)
    // 0,				/*ob_size*/
    "_gammu.StateMachine",			/*tp_name*/
    sizeof(StateMachineObject),		/*tp_basicsize*/
    0,				/*tp_itemsize*/
    /* methods */
    (destructor)StateMachine_dealloc,	/*tp_dealloc*/
    (printfunc)0,		/*tp_print*/
#if 0
    (getattrfunc)StateMachine_getattr,	/*tp_getattr*/
    (setattrfunc)StateMachine_setattr,	/*tp_setattr*/
#endif
    0,	/*tp_getattr*/
    0,	/*tp_setattr*/
    0,
#if 0
	(cmpfunc)StateMachine_compare,		/*tp_compare*/
#endif
    0,
#if 0
	(reprfunc)StateMachine_repr,		/*tp_repr*/
#endif
    0,			/*tp_as_number*/
    0,		/*tp_as_sequence*/
    0,		/*tp_as_mapping*/
    (hashfunc)0,		/*tp_hash*/
    (ternaryfunc)0,		/*tp_call*/
    0,
#if 0
	(reprfunc)StateMachine_str,		/*tp_str*/
#endif
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
    StateMachineType__doc__, /* Documentation string */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    StateMachine_methods,             /* tp_methods */
    0,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)StateMachine_init,      /* tp_init */
    0,                         /* tp_alloc */
    StateMachine_new,          /* tp_new */
    NULL,                      /* tp_free */
    0,                         /* tp_is_gc */
	0,                         /* tp_bases */
	0,                         /* tp_mro */
	0,                         /* tp_cache */
	0,                         /* tp_subclasses */
	0,                         /* tp_weaklist */
	0,                          /* tp_del */
#if PY_MAJOR_VERSION >= 2 && PY_MINOR_VERSION >= 6
    0,                          /* tp_version_tag */
#endif
};

/* ----------------------------------------------------- */



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

    if (PyModule_AddObject(m, "StateMachine", (PyObject *)&StateMachineType) < 0)
        return NULL;

    /* SMSD object */
    if (!gammu_smsd_init(m)) return NULL;

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
