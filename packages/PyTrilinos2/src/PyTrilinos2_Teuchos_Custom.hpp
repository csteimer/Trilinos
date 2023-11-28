// @HEADER
// ***********************************************************************
//
//          PyTrilinos2: Automatic Python Interfaces to Trilinos Packages
//                 Copyright (2022) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia
// Corporation, the U.S. Government retains certain rights in this
// software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Kim Liegeois (knliege@sandia.gov)
//
// ***********************************************************************
// @HEADER

#ifndef PYTRILINOS2_TEUCHOS_CUSTOM
#define PYTRILINOS2_TEUCHOS_CUSTOM

#include <Teuchos_ParameterList.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <Teuchos_CommHelpers.hpp>
#include <Teuchos_DefaultComm.hpp>
#include <mpi4py/mpi4py.h>

#if PY_VERSION_HEX >= 0x03000000

#define PyClass_Check(obj) PyObject_IsInstance(obj, (PyObject *)&PyType_Type)
#define PyInt_Check(x) PyLong_Check(x)
#define PyInt_AsLong(x) PyLong_AsLong(x)
#define PyInt_FromLong(x) PyLong_FromLong(x)
#define PyInt_FromSize_t(x) PyLong_FromSize_t(x)
#define PyString_Check(name) PyBytes_Check(name)
#define PyString_FromString(x) PyUnicode_FromString(x)
#define PyString_FromStringAndSize(x,s) PyUnicode_FromStringAndSize(x,s)
#define PyString_Format(fmt, args)  PyUnicode_Format(fmt, args)
#define PyString_AsString(str) PyBytes_AsString(str)
#define PyString_Size(str) PyBytes_Size(str)    
#define PyString_InternFromString(key) PyUnicode_InternFromString(key)
#define Py_TPFLAGS_HAVE_CLASS Py_TPFLAGS_BASETYPE
#define PyString_AS_STRING(x) PyUnicode_AS_STRING(x)
#define PyObject_Compare(x, y) (1-PyObject_RichCompareBool(x, y, Py_EQ))
#define _PyLong_FromSsize_t(x) PyLong_FromSsize_t(x)
#define convertPyStringToChar(pyobj) PyBytes_AsString(PyUnicode_AsASCIIString(pyobj))
#else
#define convertPyStringToChar(pyobj) PyString_AsString(pyobj)
#endif

namespace py = pybind11;

template<typename T>
Teuchos::Array< T > copyNumPyToTeuchosArray(pybind11::array_t<T> array) {

    auto np_array = array.template mutable_unchecked<1>();
    int size = array.shape(0);
    Teuchos::Array< T > av(size);
    for (int i=0; i < size; ++i)
      av[i] = np_array(i);
    return av;
}

template<typename T>
pybind11::array_t<T> copyTeuchosArrayToNumPy(Teuchos::Array< T > & tArray) {

    pybind11::array_t<T> array(tArray.size());
    auto data = array.template mutable_unchecked<1>();
    for (int i=0; i < tArray.size(); ++i)
      data(i) = tArray[i];
    return array;
}

// Implementation based on:
// https://github.com/trilinos/Trilinos/tree/master/packages/PyTrilinos/src/PyTrilinos_Teuchos_Util.cpp
bool setPythonParameter(Teuchos::RCP<Teuchos::ParameterList> plist,
			const std::string      & name,
			py::object             value);


template <typename T>
bool setPythonParameterArray(Teuchos::RCP<Teuchos::ParameterList> plist,
			const std::string      & name,
			pybind11::array_t< T >   value)
{
  auto tArray = copyNumPyToTeuchosArray(value);
  plist->set(name, tArray);
  return true;
}

// Implementation based on:
// https://github.com/trilinos/Trilinos/tree/master/packages/PyTrilinos/src/PyTrilinos_Teuchos_Util.cpp
py::object getPythonParameter(Teuchos::RCP<Teuchos::ParameterList> plist,
			      const std::string            & name);

template <typename T>
void def_Teuchos_functions(T m) {
  m.def("getTeuchosComm", [](pybind11::object py_obj) -> Teuchos::RCP<const Teuchos::Comm<int> > {
    if (import_mpi4py() < 0) {
      throw pybind11::error_already_set();
    }
    auto py_src = py_obj.ptr();
    if (PyObject_TypeCheck(py_src, &PyMPIComm_Type)) {
      return Teuchos::rcp<const Teuchos::MpiComm<int>>(new Teuchos::MpiComm<int>
        (Teuchos::opaqueWrapper(*PyMPIComm_Get(py_src))));
    }
    else
      return Teuchos::null;
  }, "");
}

template <typename T>
void def_ParameterList_member_functions(T cl) {
  cl.def("__setitem__", [](Teuchos::RCP<Teuchos::ParameterList> &m, const std::string &name, Teuchos::ParameterList value) { m->set(name,value);  });
  cl.def("set", [](Teuchos::RCP<Teuchos::ParameterList> &m, const std::string &name, Teuchos::ParameterList value) { m->set(name,value);  });
  cl.def("sublist", [](Teuchos::RCP<Teuchos::ParameterList> &m, const std::string &name) { if (m->isSublist(name)) { return pybind11::cast(sublist(m, name)); } return pybind11::cast("Invalid sublist name"); }, pybind11::return_value_policy::reference);
  cl.def("__setitem__", [](Teuchos::RCP<Teuchos::ParameterList> &m, const std::string &name, pybind11::object value) { setPythonParameter(m,name,value);  });
  cl.def("__getitem__", [](Teuchos::RCP<Teuchos::ParameterList> &m, const std::string &name) {
    // Sublist
    if (m->isSublist(name))
      return py::cast(Teuchos::sublist(m, name));
    return getPythonParameter(m,name);
  });
  cl.def("set", [](Teuchos::RCP<Teuchos::ParameterList> &m, const std::string &name, pybind11::object value) { setPythonParameter(m,name,value);  });
  cl.def("get", [](Teuchos::RCP<Teuchos::ParameterList> &m, const std::string &name) {
    // Sublist
    if (m->isSublist(name))
      return py::cast(Teuchos::sublist(m, name));
    return getPythonParameter(m,name);
  });
}
#endif // PYTRILINOS2_TEUCHOS_CUSTOM