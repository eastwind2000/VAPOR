#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include <time.h>
#ifdef  Darwin
#include <mach/mach_time.h>
#endif
#ifdef _WINDOWS
#include "windows.h"
#include "Winbase.h"
#include <limits>
#endif

#include <vapor/utils.h>
#include <vapor/StructuredGrid.h>

using namespace std;
using namespace VAPoR;

StructuredGrid::StructuredGrid(
	const vector <size_t> &dims,
	const vector <size_t> &bs,
	const vector <float *> &blks
) : Grid(dims, bs, blks, dims.size()) {

	assert(bs.size() == 2 || bs.size() == 3);

}

bool StructuredGrid::GetCellNodes(
    const std::vector <size_t> &cindices,
    std::vector <vector <size_t> > &nodes
) const {
	nodes.clear();

	vector <size_t> dims = GetDimensions();
	assert (cindices.size() == dims.size());

	// Check if invalid indices
	//
	for (int i=0; i<cindices.size(); i++) {
		if (cindices[i] > (dims[i]-2)) return(false);
	}

	// Cells have the same ID's as their first node
	//
	// walk counter-clockwise order
	//
	vector <size_t> indices;

	if (dims.size() == 2) {
		indices =  {cindices[0], cindices[1]};
		nodes.push_back(indices);

		indices =  {cindices[0]+1, cindices[1]};
		nodes.push_back(indices);

		indices =  {cindices[0]+1, cindices[1]+1};
		nodes.push_back(indices);

		indices =  {cindices[0], cindices[1]+1};
		nodes.push_back(indices);

	}
	else if (dims.size() == 3 && dims[2] > 1) {
		indices =  {cindices[0], cindices[1], cindices[2]};
		nodes.push_back(indices);

		indices =  {cindices[0]+1, cindices[1], cindices[2]};
		nodes.push_back(indices);

		indices =  {cindices[0]+1, cindices[1]+1, cindices[2]};
		nodes.push_back(indices);

		indices =  {cindices[0], cindices[1]+1, cindices[2]};
		nodes.push_back(indices);

		indices =  {cindices[0], cindices[1], cindices[2]+1};
		nodes.push_back(indices);

		indices =  {cindices[0]+1, cindices[1], cindices[2]+1};
		nodes.push_back(indices);

		indices =  {cindices[0]+1, cindices[1]+1, cindices[2]+1};
		nodes.push_back(indices);

		indices =  {cindices[0], cindices[1]+1, cindices[2]+1};
		nodes.push_back(indices);
	}

	return(true);
}

bool StructuredGrid::GetCellNeighbors(
    const std::vector <size_t> &cindices,
    std::vector <vector <size_t> > &cells
) const {
	cells.clear();

	vector <size_t> dims = GetDimensions();
	assert (cindices.size() == dims.size());

	assert((dims.size() == 2) && "3D cells not yet supported");

	// Check if invalid indices
	//
	for (int i=0; i<cindices.size(); i++) {
		if (cindices[i] > (dims[i]-2)) return(false);
	}

	// Cells have the same ID's as their first node
	//
	// walk counter-clockwise order
	//
	if (dims.size() == 2) {
		vector <size_t> indices;

		if (cindices[1] != 0) {	// below
			indices = {cindices[0], cindices[1]-1};
		}
		cells.push_back(indices);

		if (cindices[0] != dims[0]-2) {	// right
			indices = {cindices[0]+1, cindices[1]};
		}
		cells.push_back(indices);

		if (cindices[1] != dims[1]-2) {	// top
			indices = {cindices[0], cindices[1]+1};
		}
		cells.push_back(indices);

		if (cindices[0] != 0) {	// left
			indices = {cindices[0]-1, cindices[1]};
		}
		cells.push_back(indices);
	}
	return(true);
}

bool StructuredGrid::GetNodeCells(
    const std::vector <size_t> &indices,
    std::vector <vector <size_t> > &cells
) const {
	cells.clear();

	vector <size_t> dims = GetDimensions();
	assert (indices.size() == dims.size());

	assert((dims.size() == 2) && "3D cells not yet supported");

	// Check if invalid indices
	//
	for (int i=0; i<indices.size(); i++) {
		if (indices[i] > (dims[i]-1)) return(false);
	}

	if (dims.size() == 2) {
		vector <size_t> indices;

		if (indices[0] != 0 && indices[1] != 0) {	// below, left
			indices = {indices[0]-1, indices[1]-1};
			cells.push_back(indices);
		}

		if (indices[1] != 0) {	// below, right
			indices = {indices[0], indices[1]-1};
			cells.push_back(indices);
		}

		if (indices[0] != (dims[0]-1) && indices[1] != (dims[1])) {// top, right
			indices = {indices[0], indices[1]};
			cells.push_back(indices);
		}

		if (indices[0] != 0) {// top, top
			indices = {indices[0]-1, indices[1]};
			cells.push_back(indices);
		}
	}
	return(true);

}

void StructuredGrid::ClampCoord(std::vector <double> &coords) const {
	assert(coords.size() >= GetTopologyDim());

	while (coords.size() > GetTopologyDim()) {
		coords.pop_back();
	}

	vector <bool> periodic = GetPeriodic();
	vector <size_t> dims = GetDimensions();

	vector <double> minu, maxu;
	GetUserExtents(minu, maxu);

	for (int i=0; i<coords.size(); i++) {

		//
		// Handle coordinates for dimensions of length 1
		//
		if (dims[i] == 1) {
			coords[i] = minu[i];
			continue;
		}

		if (coords[i]<minu[i] && periodic[i]) {
			while (coords[i]<minu[i]) coords[i]+= maxu[i]-minu[i];
		}
		if (coords[i]>maxu[i] && periodic[i]) {
			while (coords[i]>maxu[i]) coords[i]-= maxu[i]-minu[i];
		}

	}
}



#ifdef	DEAD

// Only partially implmented for 2D cells. Need predicate cell-intersect-box
// functor
//
//	Assumes counter-clockwise node ordering:
//
//	3-------2
//	|       |	
//	|       |	
//	|       |	
//	0-------1
//  
//
template <class T>
StructuredGrid::CellIterator<T>::CellIterator (
	T *sg, const vector <double> &minu, const vector <double> &maxu
) : _pred(minu, maxu) {

	_sg = sg;
	_dims = rg->GetDimensions();

	_cellIndex = vector <size_t> (_dims.size(), 0);

	_coordItr0 = rg->begin();
	_coordItr1 = rg->begin();
	++_coordItr1;

	_coordItr2 = rg->begin();
	_coordItr3 = rg->begin();
	++_coordItr2;

	for (int i=0; i<_dims[0]; i++) {
		++_coordItr2
		++_coordItr3
	}

	// Skip to first cell containing point (if any)
	//
	if (! _pred(*_coordItr0, *_coordItr1, *_coordItr2, *_coordItr3)) {
		operator++();
	}
}
#endif

template <class T>
StructuredGrid::ForwardCellIterator<T>::ForwardCellIterator(T *sg, bool begin) {
	_sg = sg;
	_dims = sg->GetDimensions();

	_cellIndex = vector <size_t> (_dims.size(), 0);
	if (! begin) {
		_cellIndex[_dims.size()-1] = _dims[_dims.size()-1] - 1;
    }
}

template <class T>
StructuredGrid::ForwardCellIterator<T>::ForwardCellIterator(
	ForwardCellIterator<T> &&rhs
) {

	_sg = rhs._sg; rhs._sg = nullptr;
	_dims = rhs._dims;
	_cellIndex = rhs._cellIndex;

#ifdef	DEAD
	_coordItr0 = std::move(rhs._coordItr0);
	_coordItr1 = std::move(rhs._coordItr1);
	_coordItr2 = std::move(rhs._coordItr2);
	_coordItr3 = std::move(rhs._coordItr3);
	_coordItr4 = std::move(rhs._coordItr4);
	_coordItr5 = std::move(rhs._coordItr5);
	_coordItr6 = std::move(rhs._coordItr6);
	_coordItr7 = std::move(rhs._coordItr7);
#endif

}

template <class T>
StructuredGrid::ForwardCellIterator<T>::ForwardCellIterator() {
	_sg = nullptr;
	_dims.clear();
	_cellIndex.clear();
}

template <class T>
StructuredGrid::ForwardCellIterator<T>
&StructuredGrid::ForwardCellIterator<T>::operator=(ForwardCellIterator<T> rhs) {

	swap(*this, rhs);
	return(*this);
}


template <class T>
StructuredGrid::ForwardCellIterator<T>
&StructuredGrid::ForwardCellIterator<T>::next2d() {

	if (_cellIndex[1] >= (_dims[1]-1)) return(*this);

	_cellIndex[0]++;

#ifdef	DEAD
	++_coordItr1;
	++_coordItr2;
	++_coordItr3;
	++_coordItr4;
#endif

	if (_cellIndex[0]<(_dims[0]-1)) {
		return(*this);
	}

	if (_cellIndex[0] >= (_dims[0]-1)) {

#ifdef	DEAD
		++_coordItr1;
		++_coordItr2;
		++_coordItr3;
		++_coordItr4;
#endif
		_cellIndex[0] = 0;
		_cellIndex[1]++;
	}
	return(*this);
}

template <class T>
StructuredGrid::ForwardCellIterator<T>
&StructuredGrid::ForwardCellIterator<T>::next3d() {

	if (_cellIndex[2] >= (_dims[2]-1)) return(*this);

	_cellIndex[0]++;

#ifdef	DEAD
	++_coordItr1;
	++_coordItr2;
	++_coordItr3;
	++_coordItr4;
#endif

	if (_cellIndex[0]<(_dims[0]-1)) {
		return(*this);
	}

	if (_cellIndex[0] >= (_dims[0]-1)) {
#ifdef	DEAD
		++_coordItr1;
		++_coordItr2;
		++_coordItr3;
		++_coordItr4;
#endif
		_cellIndex[0] = 0;
		_cellIndex[1]++;
	}

	if (_cellIndex[1] >= (_dims[1]-1)) {
#ifdef	DEAD
		++_coordItr1;
		++_coordItr2;
		++_coordItr3;
		++_coordItr4;
#endif
		_cellIndex[1] = 0;
		_cellIndex[2]++;
	}

	return(*this);
}

template <class T> 
StructuredGrid::ForwardCellIterator<T>
&StructuredGrid::ForwardCellIterator<T>::operator++() {

	if (_dims.size() == 2) return(next2d());
	if (_dims.size() == 3) return(next3d());

	assert(_dims.size() >=2 && _dims.size() <= 3);
	return(*this);
}

// Need this so that template definitions can be made in .cpp file, not .h file
//
template class StructuredGrid::ForwardCellIterator<const StructuredGrid>;




#ifdef	DEAD

// Only partially implmented for 2D node. Need predicate node-intersect-box
// functor
//
//
template <class T>
StructuredGrid::NodeIterator<T>::NodeIterator (
	T *sg, const vector <double> &minu, const vector <double> &maxu
) : _pred(minu, maxu) {

	_sg = sg;
	_dims = rg->GetDimensions();

	_cellIndex = vector <size_t> (_dims.size(), 0);


	....
}
#endif

template <class T>
StructuredGrid::ForwardNodeIterator<T>::ForwardNodeIterator(T *sg, bool begin) {
	_sg = sg;
	_dims = sg->GetDimensions();

	_nodeIndex = vector <size_t> (_dims.size(), 0);
	if (! begin) {
		_nodeIndex[_dims.size()-1] = _dims[_dims.size()-1];
    }
}

template <class T>
StructuredGrid::ForwardNodeIterator<T>::ForwardNodeIterator(
	ForwardNodeIterator<T> &&rhs
) {

	_sg = rhs._sg; rhs._sg = nullptr;
	_dims = rhs._dims;
	_nodeIndex = rhs._nodeIndex;

}

template <class T>
StructuredGrid::ForwardNodeIterator<T>::ForwardNodeIterator() {
	_sg = nullptr;
	_dims.clear();
	_nodeIndex.clear();
}

template <class T>
StructuredGrid::ForwardNodeIterator<T>
&StructuredGrid::ForwardNodeIterator<T>::operator=(ForwardNodeIterator<T> rhs) {

	swap(*this, rhs);
	return(*this);
}


template <class T>
StructuredGrid::ForwardNodeIterator<T>
&StructuredGrid::ForwardNodeIterator<T>::next2d() {

	if (_nodeIndex[1] >= (_dims[1])) return(*this);

	_nodeIndex[0]++;

	if (_nodeIndex[0]<(_dims[0])) {
		return(*this);
	}

	if (_nodeIndex[0] >= (_dims[0])) {

		_nodeIndex[0] = 0;
		_nodeIndex[1]++;
	}
	return(*this);
}

template <class T>
StructuredGrid::ForwardNodeIterator<T>
&StructuredGrid::ForwardNodeIterator<T>::next3d() {

	if (_nodeIndex[2] >= (_dims[2]-1)) return(*this);

	_nodeIndex[0]++;

#ifdef	DEAD
	++_coordItr1;
	++_coordItr2;
	++_coordItr3;
	++_coordItr4;
#endif

	if (_nodeIndex[0]<(_dims[0])) {
		return(*this);
	}

	if (_nodeIndex[0] >= (_dims[0])) {
		_nodeIndex[0] = 0;
		_nodeIndex[1]++;
	}

	if (_nodeIndex[1] >= (_dims[1])) {
		_nodeIndex[1] = 0;
		_nodeIndex[2]++;
	}

	return(*this);
}

template <class T> 
StructuredGrid::ForwardNodeIterator<T>
&StructuredGrid::ForwardNodeIterator<T>::operator++() {

	if (_dims.size() == 2) return(next2d());
	if (_dims.size() == 3) return(next3d());

	assert(_dims.size() >=2 && _dims.size() <= 3);
	return(*this);
}

// Need this so that template definitions can be made in .cpp file, not .h file
//
template class StructuredGrid::ForwardNodeIterator<const StructuredGrid>;




namespace VAPoR {
std::ostream &operator<<(std::ostream &o, const StructuredGrid &sg)
{
	o << "StructuredGrid " << endl;
	o << endl;

	o << (const Grid &) sg;

	return o;
}
};


