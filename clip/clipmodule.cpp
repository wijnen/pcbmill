#include <Python.h>
#include <polyclipping/clipper.hpp>
#include <list>

using namespace ClipperLib;

#define Size Py_ssize_t
#define len PySequence_Length
#define get PySequence_GetItem
#define check PySequence_Check

typedef std::list <Paths> Board;

static bool debug = false;

static void dump_paths(Paths const &paths) {
	for (size_t i = 0; i < paths.size(); ++i) {
		for (size_t p = 0; p < paths[i].size(); ++p) {
			printf("%f\t%f\n", paths[i][p].X * 1. / (1ll << 32), paths[i][p].Y * 1. / (1ll << 32));
		}
		printf("\n");
	}
}

static void merge(Board &result, Paths paths) {
	for (Board::iterator i = result.begin(); i != result.end(); ++i) {
		Clipper clip;
		clip.AddPaths(*i, ptSubject, true);
		clip.AddPaths(paths, ptClip, true);
		Paths p;
		clip.Execute(ctIntersection, p, pftEvenOdd, pftEvenOdd);
		if (p.size() == 0)
			continue;
		clip.Execute(ctUnion, p, pftEvenOdd, pftEvenOdd);
		result.erase(i);
		return merge(result, p);
	}
	result.push_back(paths);
}

static bool read_data(Board &result, PyObject *regions) {
	Size num_regions = len(regions);
	for (Size r = 0; r < num_regions; ++r) {
		PyObject *region = get(regions, r);
		if (!check(region)) {
			PyErr_SetString(PyExc_ValueError, "Regions must be sequences.");
			return false;
		}
		Size numpoints = len(region);
		Paths path(1);
		path[0] = Path(numpoints);
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
			path[0][p].X = coordinate[0];
			path[0][p].Y = coordinate[1];
		}
		merge(result, path);
	}
	return true;
}

static void apply_offset(Board &result, double offset) {
	for (Board::iterator i = result.begin(); i != result.end(); ++i) {
		ClipperOffset offsetter(2ll << 32, 1ll << 30);
		offsetter.AddPaths(*i, jtRound, etClosedPolygon);
		Paths offset_result;
		// Multiply offset by 2, because half of it is inside the shape.
		offsetter.Execute(offset_result, offset * (2ll << 32));
		Clipper clip;
		clip.AddPaths(*i, ptSubject, true);
		clip.AddPaths(offset_result, ptClip, true);
		Paths solution;
		clip.Execute(ctUnion, *i, pftEvenOdd, pftEvenOdd);
	}
}

static bool try_offset(Board &paths, double offset) {
	apply_offset(paths, offset);
	// Check zero intersections.
	Paths check;
	for (Board::iterator i = paths.begin(); i != paths.end(); ++i) {
		Paths intersection;
		Clipper clip;
		clip.AddPaths(check, ptSubject, true);
		clip.AddPaths(*i, ptClip, true);
		clip.Execute(ctIntersection, intersection, pftEvenOdd, pftEvenOdd);
		if (intersection.size() > 0) {
			if (debug)
				dump_paths(intersection);
			return false;
		}
		clip.Execute(ctUnion, check, pftEvenOdd, pftEvenOdd);
	}
	return true;
}

static PyObject *make_output(Board &result) {
	size_t total_num_paths = 0;
	for (Board::iterator i = result.begin(); i != result.end(); ++i)
		total_num_paths += i->size();
	PyObject *ret = PyTuple_New(total_num_paths);
	size_t base = 0;
	for (Board::iterator i = result.begin(); i != result.end(); ++i) {
		for (size_t r = 0; r < i->size(); ++r) {
			PyObject *path = PyTuple_New((*i)[r].size());
			for (size_t p = 0; p < (*i)[r].size(); ++p) {
				PyObject *point = Py_BuildValue("(dd)", (*i)[r][p].X * 1. / (1ll << 32), (*i)[r][p].Y * 1. / (1ll << 32));
				PyTuple_SetItem(path, p, point);
			}
			PyTuple_SetItem(ret, base + r, path);
		}
		base += i->size();
	}
	return ret;
}

static PyObject *clip_handle(PyObject *self, PyObject *args) {
	PyObject *regions;
	double offset;
	if (!PyArg_ParseTuple(args, "Od", &regions, &offset))
		return NULL;
	if (offset < 0) {
		debug = true;
		offset = -offset;
	}
	Board result;
	if (!read_data(result, regions))
		return NULL;
	if (debug) {
		for (Board::iterator i = result.begin(); i != result.end(); ++i)
			dump_paths(*i);
	}
	Board original = result;
	if (!try_offset(result, offset)) {
		// Binary search.
		double h = offset;
		double l = 0;
		double epsilon = 1e-5;
		while (h - l > epsilon) {
			offset = (h + l) / 2;
			printf("binary search offset: %f\n", offset);
			result = original;
			if (try_offset(result, offset))
				l = offset;
			else
				h = offset;
		}
	}
	return make_output(result);
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
