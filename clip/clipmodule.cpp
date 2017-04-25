// vim: set foldmethod=marker :
// Header. {{{
#include <Python.h>
#include <polyclipping/clipper.hpp>
#include <list>
#include <stdio.h>

using namespace ClipperLib;

#define Size Py_ssize_t
#define len PySequence_Length
#define get PySequence_GetItem
#define check PySequence_Check

typedef std::list <Paths> Board;

static bool debug = false;
// }}}

static void dump_paths(Paths const &paths, int code) { // {{{
	for (size_t i = 0; i < paths.size(); ++i) {
		for (size_t p = 0; p < paths[i].size(); ++p) {
			printf("%f\t%f\t%d\t%ld\n", paths[i][p].X * 1. / (1ll << 32), paths[i][p].Y * 1. / (1ll << 32), code, i);
		}
		printf("\n");
	}
} // }}}

// Use clipper library to merge two paths.
static void merge(Board &result, Paths paths) { // {{{
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
} // }}}

// Read the Python data and merge all the regions into one Paths object.
static bool read_data(Board &result, PyObject *regions, PyObject *mask, Paths &maskpaths) { // {{{
	// Read mask path, if any.
	if (mask != Py_None) {
		for (Size m = 0; m < len(mask); ++m) {
			PyObject *region = get(mask, m);
			maskpaths.push_back(Path(len(region)));
			for (Size p = 0; p < len(region); ++p) {
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
				maskpaths.back()[p].X = coordinate[0];
				maskpaths.back()[p].Y = coordinate[1];
			}
		}
	}
	Size num_regions = len(regions);
	for (Size r = 0; r < num_regions; ++r) {
		fprintf(stderr, "Handling region %ld/%ld\r", r + 1, num_regions);
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
	if (maskpaths.size() > 0) {
		for (Board::iterator i = result.begin(); i != result.end(); ++i) {
			for (Paths::iterator k = i->begin(); k != i->end(); ++k) {
				Clipper clip;
				clip.AddPath(*k, ptSubject, true);
				clip.AddPaths(maskpaths, ptClip, true);
				Paths p;
				clip.Execute(ctDifference, p, pftEvenOdd, pftEvenOdd);
				if (p.size() != 0) {
					if (debug)
						printf("skipping path which has parts outside clipping mask");
					k->clear();
					continue;
				}
			}
		}
	}
	return true;
} // }}}

// Apply offset to the board.
static void apply_offset(Board &result, double offset) { // {{{
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
} // }}}

// Check if requested offset is valid.
static bool try_offset(Board &paths, double offset) { // {{{
	apply_offset(paths, offset);
	// Check zero intersections.
	Paths check;
	int t = 0;
	for (Board::iterator i = paths.begin(); i != paths.end(); ++i, ++t) {
		Paths intersection;
		Clipper clip;
		clip.AddPaths(check, ptSubject, true);
		clip.AddPaths(*i, ptClip, true);
		clip.Execute(ctIntersection, intersection, pftEvenOdd, pftEvenOdd);
		if (intersection.size() > 0) {
			if (debug)
				dump_paths(intersection, t);
			return false;
		}
		clip.Execute(ctUnion, check, pftEvenOdd, pftEvenOdd);
	}
	return true;
} // }}}

// Generate Python objects to return.
static PyObject *make_output(Board &result) { // {{{
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
} // }}}

// Function that is called from Python.
static PyObject *clip_handle(PyObject *self, PyObject *args) { // {{{
	PyObject *regions;
	double offset;
	PyObject *mask;
	if (!PyArg_ParseTuple(args, "OdO", &regions, &offset, &mask))
		return NULL;
	if (offset < 0) {
		debug = true;
		offset = -offset;
	}
	Board result;
	Paths maskpaths;
	if (!read_data(result, regions, mask, maskpaths))
		return NULL;
	if (debug) {
		printf("initial paths\n");
		int t = 0;
		for (Board::iterator i = result.begin(); i != result.end(); ++i, ++t)
			dump_paths(*i, t);
		printf("initial paths done\n");
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
} // }}}

// Registration machinery. {{{
static PyMethodDef ClipMethods[] = { // {{{
	{"handle", clip_handle, METH_VARARGS, "Make union and offset polygons."},
	{NULL, NULL, 0, NULL}
}; // }}}

static PyModuleDef clipmodule = { // {{{
	PyModuleDef_HEAD_INIT,
	"clip",
	NULL,
	-1,
	ClipMethods
}; // }}}

PyMODINIT_FUNC PyInit_clip() { // {{{
	return PyModule_Create(&clipmodule);
} // }}}
// }}}
