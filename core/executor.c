#include <Python.h>
#include "pyhp.h"

#include "executor.h"
#include "streams.h"
#include "encoding.h"
#include <frameobject.h>

static PyHP_LOW_API PyHP_Core_Task_Object *PyHP_Core_CreateTask(void)
{
    return (PyHP_Core_Task_Object *) (PyHP_Core_Task_Type.tp_alloc(&PyHP_Core_Task_Type, 0));
}

static PyHP_LOW_API void PyHP_Core_ReleaseTask(PyHP_Core_Task *self)
{
    Py_DECREF(self->o_stdin);
    Py_DECREF(self->o_stdout);
    Py_DECREF(self->o_stderr);
}

PyHP_LOW_API int PyHP_Core_ExecEmbed(PyHP_Core_PageCode *code, PyObject *globals)
{
    PyHP_Core_Task meta = {NULL};
    Py_ssize_t i;
    Py_ssize_t len;
    PyHP_Core_PageCode_Cell cell;
    PyObject *string;
    PyObject *result;


    len = PyHP_Core_PageCode_SLEN(code);
    for (i = 0; i < len; i++)
    {
        meta.o_stdout = PySys_GetObject("stdout");

        cell = PyHP_Core_PageCode_STATEMENTS(code)[i];
        switch (cell.type)
        {
            case PyHP_Core_PageCodeType_TEXT:
                if (Py_TYPE(meta.o_stdout) == &PyHP_Core_Stdout_Type)
                {
                    PyHP_Core_Stream_Write(&(((PyHP_Core_Stdout_Object *) (meta.o_stdout))->data), PyHP_Core_PageCode_BUFFER(code), cell.value.raw.start, cell.value.raw.len);
                }
                else if (Py_TYPE(meta.o_stdout) == &PyHP_Core_Stderr_Type)
                {
                    PyHP_Core_Stream_Write(&(((PyHP_Core_Stderr_Object *) (meta.o_stdout))->data), PyHP_Core_PageCode_BUFFER(code), cell.value.raw.start, cell.value.raw.len);
                }
                else
                {

                    string = PyHP_Core_DecodeString(PyHP_Core_PageCode_BUFFER(code), cell.value.raw.start, cell.value.raw.len);
                    if (string == NULL)
                    {
                        return 1;
                    }


                    result = PyObject_CallMethod(meta.o_stdout, "write", "O", string);

                    Py_DECREF(string);
                    if (result == NULL)
                    {
                        return 1;
                    }
                    Py_DECREF(result);
                }

                break;
            case PyHP_Core_PageCodeType_EVAL:
            case PyHP_Core_PageCodeType_EXEC:

                result = PyEval_EvalCode(cell.value.code, globals, globals);
                if (result == NULL)
                {
                    return 1;
                }
                if (cell.type == PyHP_Core_PageCodeType_EVAL)
                {
                    PyObject_Print(result, stdout, Py_PRINT_RAW);
                }
                Py_DECREF(result);
                break;
        }
    }

    return 0;
}

PyHP_HIGH_API PyObject *PyHP_Core_ExecEmbed_Func(PyObject *module, PyObject *raw_code)
{
    PyHP_Core_PageCode *code;
    PyHP_Core_PageCode icode = {NULL};
    const char *src;
    Py_ssize_t len;

    if (!(Py_TYPE(raw_code) == &PyHP_Core_PageCode_Type))
    {
        if (!(PyUnicode_Check(raw_code)))
        {
            PyErr_Format(
                    PyExc_TypeError,
                    "Can compile only strings"
            );
            return NULL;
        }

        src = PyHP_Core_EncodeStringRO(raw_code, &len);
        if (src == NULL)
        {
            return NULL;

        }

        icode.name = PyUnicode_FromString("<py3hp page>");
        if (icode.name == NULL)
        {
            return NULL;

        }

        code = &icode;
        if (PyHP_Core_Compile(src, len, &(PyHP_Core_PageCode_SLEN(code)), (PyHP_Core_PageCode_Cell **) &(icode.statements), &(PyHP_Core_PageCode_BLEN(code)), (char **) &(icode.buffer), -1) != 0)
        {
            Py_DECREF(icode.name);
            return NULL;

        }
    }
    else
    {
        code = &((PyHP_Core_PageCode_Object *) raw_code)->data;
    }

    if (PyHP_Core_ExecEmbed(code, PyEval_GetGlobals()) != 0)
    {
        goto err;
    }

    if (icode.name != NULL)
    {
        PyHP_Core_ReleasePageCode(code);
        PyHP_Core_ReleaseCompile((PyHP_Core_PageCode_Cell *) (icode.statements), (char *) (icode.buffer));
    }

    Py_RETURN_NONE;
    err:
    if (icode.name != NULL)
    {
        PyHP_Core_ReleasePageCode(code);
        PyHP_Core_ReleaseCompile((PyHP_Core_PageCode_Cell *) (icode.statements), (char *) (icode.buffer));
    }

    return NULL;

}

PyHP_LOW_API int PyHP_Core_Exec(PyHP_Core_Task *meta, PyObject *globals)
{
    PyThreadState *interpreter;
    interpreter = Py_NewInterpreter();
    if (interpreter == NULL)
    {
        return 1;
    }
    meta->interpreter = interpreter;
    if (PyThreadState_Get() != interpreter)
    {
        PyThreadState_Swap(interpreter);
    }


    if (PySys_SetObject("stdin", meta->o_stdin) != 0)
    {
        goto err;
    }
    if (PySys_SetObject("stdout", meta->o_stdout) != 0)
    {
        goto err;
    }
    if (PySys_SetObject("stderr", meta->o_stdout) != 0)
    {
        goto err;
    }

    if (PyEval_GetGlobals() != NULL)
    {

        if (PyDict_Update(globals, PyEval_GetGlobals()) != 0)
        {
            goto err;
        }
    }
    if (PyDict_Merge(globals, PyEval_GetBuiltins(), 0) != 0)
    {
        goto err;
    }


    if (PyHP_Core_ExecEmbed(meta->code, globals) != 0)
    {
        goto err;
    }


    Py_EndInterpreter(interpreter);
    PyThreadState_Swap(meta->parent);
    return 0;
    err:
    Py_EndInterpreter(interpreter);
    PyThreadState_Swap(meta->parent);
    return 1;

}

PyHP_HIGH_API PyObject *PyHP_Core_Exec_Func(PyObject *module, PyObject *args, PyObject *kwargs)
{
    static char *kw_list[] = {"", "globals", "stdin", "stdout", "stderr", NULL};
    PyObject *source;
    PyObject *globals;
    PyHP_Core_Task meta = {NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!O!OOO", kw_list, &PyHP_Core_PageCode_Type, &source, &PyDict_Type, &globals, &(meta.o_stdin), &(meta.o_stdout), &(meta.o_stderr)))
    {
        return NULL;
    }

    meta.parent = PyThreadState_Get();
    meta.code = &(((PyHP_Core_PageCode_Object *) source)->data);
    if (PyHP_Core_Exec(&meta, globals) != 0)
    {
        return NULL;
    }

    Py_RETURN_NONE;

}

static PyHP_HIGH_API void PyHP_Core_Task_Dealloc(PyHP_Core_Task_Object *self)
{
    PyHP_Core_ReleaseTask(&(self->data));
    Py_TYPE(self)->tp_free(self);
}

PyTypeObject PyHP_Core_Task_Type = {
        PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = "py3hp.core.task",
        .tp_dealloc = (destructor) PyHP_Core_Task_Dealloc,
};