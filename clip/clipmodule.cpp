#include <Python.h>
#include <polyclipping/clipper.hpp>

using namespace ClipperLib;

#define Size Py_ssize_t
#define len PySequence_Length
#define get PySequence_GetItem
#define check PySequence_Check

static void dump_paths(Paths const &paths) {
	for (unsigned int i = 0; i < paths.size(); ++i) {
		for (unsigned int p = 0; p < paths[i].size(); ++p) {
			printf("%f\t%f\n", paths[i][p].X * 1. / (1ll << 32), paths[i][p].Y * 1. / (1ll << 32));
		}
		printf("\n");
	}
}

static bool read_data(Paths &result, PyObject *regions) {
	Size num_regions = len(regions);
	for (Size r = 0; r < num_regions; ++r) {
		PyObject *region = get(regions, r);
		if (!check(region)) {
			PyErr_SetString(PyExc_ValueError, "Regions must be sequences.");
			return false;
		}
		Size numpoints = len(region);
		Path path = Path(numpoints);
		for (Size p = 0; p < numpoints; ++p) {
			PyObject *point = get(region, p);
			if (!check(point) || len(point) != 2) {
				PyErr_SetString(PyExc_ValueError, "Points must be sequences of length 2.");
				return false;
			}
			cInt coordinate[2];
			for (int c = 0; c < 2; ++c) {
				PyObject *oc;
				oc = get(point, c);
				if (!PyNumber_Check(oc)) {
					PyErr_SetString(PyExc_ValueError, "Point elements must be numbers.");
					return false;
				}
				coordinate[c] = static_cast <cInt> (PyFloat_AsDouble(PyNumber_Float(oc)) * (1ll << 32));
			}
			path[p].X = coordinate[0];
			path[p].Y = coordinate[1];
		}
		Clipper clip;
		clip.AddPaths(result, ptSubject, true);
		clip.AddPath(path, ptClip, true);
		clip.Execute(ctUnion, result, pftNonZero, pftNonZero);
	}
	return true;
}

static bool apply_offset(Paths &result, double offset) {
	for (size_t r = 0; r < result.size(); ++r) {
		ClipperOffset offsetter(2ll << 32, 1ll << 30);
		offsetter.AddPath(result[r], jtRound, etClosedPolygon);
		Paths offset_result;
		// Multiply offset by 2, because half of it is inside the shape.
		offsetter.Execute(offset_result, offset * (2ll << 32));
		Clipper clip;
		clip.AddPath(result[r], ptSubject, true);
		clip.AddPaths(offset_result, ptClip, true);
		Paths solution;
		clip.Execute(ctUnion, solution, pftEvenOdd, pftEvenOdd);
		if (solution.size() != 1) {
			PyErr_SetString(PyExc_ValueError, "Offset results in multiple paths; should not be possible. Please report as a bug.");
			return false;
		}
		result[r] = solution[0];
	}
	return true;
}

static PyObject *clip_handle(PyObject *self, PyObject *args) {
	PyObject *regions;
	double offset;
	if (!PyArg_ParseTuple(args, "Od", &regions, &offset))
		return NULL;
	Paths result;
	if (!read_data(result, regions))
		return NULL;
	//Paths original = result;
	if (!apply_offset(result, offset))
		return NULL;
	// Check zero intersections.
	Paths check;
	for (size_t r = 0; r < result.size(); ++r) {
		Paths intersection;
		Clipper clip;
		clip.AddPaths(check, ptSubject, true);
		clip.AddPath(result[r], ptClip, true);
		clip.Execute(ctIntersection, intersection, pftEvenOdd, pftEvenOdd);
		if (intersection.size() > 0) {
			dump_paths(intersection);
			PyErr_SetString(PyExc_ValueError, "Offset causes regions to overlap.  Use a smaller offset or give the design more clearance.");
			return NULL;
		}
		clip.Execute(ctUnion, check, pftEvenOdd, pftEvenOdd);
	}
	PyObject *ret = PyTuple_New(result.size());
	for (size_t r = 0; r < result.size(); ++r) {
		PyObject *path = PyTuple_New(result[r].size());
		for (size_t p = 0; p < result[r].size(); ++p) {
			PyObject *point = Py_BuildValue("(dd)", result[r][p].X * 1. / (1ll << 32), result[r][p].Y * 1. / (1ll << 32));
			PyTuple_SetItem(path, p, point);
		}
		PyTuple_SetItem(ret, r, path);
	}
	return ret;
}

static PyMethodDef ClipMethods[] = {
	{"handle", clip_handle, METH_VARARGS, "Make union and offset polygons."},
	{NULL, NULL, 0, NULL}
};

static PyModuleDef clipmodule = {
	PyModuleDef_HEAD_INIT,
	"clip",
	NULL,
	-1,
	ClipMethods
};

PyMODINIT_FUNC PyInit_clip() {
	return PyModule_Create(&clipmodule);
}
