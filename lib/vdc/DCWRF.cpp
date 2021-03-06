#include <vector>
#include <algorithm>
#include <map>
#include <iostream>
#include <cassert>
#include <stdio.h>

#ifdef _WINDOWS
#define _USE_MATH_DEFINES
#pragma warning(disable : 4251 4100)
#endif
#include <cmath>

#include <vapor/GeoUtil.h>
#include <vapor/UDUnitsClass.h>
#include <vapor/DCUtils.h>
#include <vapor/DCWRF.h>
#include <vapor/DerivedVar.h>
#include <netcdf.h>

using namespace VAPoR;
using namespace std;

namespace {
#ifdef UNUSED_FUNCTION
bool mycompare(const pair<int, float>  &a, const pair<int, float>  &b) {
	return(a.second < b.second);
}
#endif
};

DCWRF::DCWRF() {
	_ncdfc = NULL;

	_dx = -1.0;
	_dy = -1.0;
	_cen_lat = 0.0;
	_cen_lon = 0.0;
	_pole_lat = 90.0;
	_pole_lon = 0.0;
	_grav = 9.81;
	_radius = 0.0;
	_p2si = 1.0;
	_mapProj = 0;

	_proj4String.clear();

	_derivedVars.clear();
	_derivedTime = NULL;

	_dimsMap.clear();
	_coordVarsMap.clear();
	_dataVarsMap.clear();
	_meshMap.clear();


}

DCWRF::~DCWRF() {

	for (int i=0; i<_derivedVars.size(); i++) {
		if (_derivedVars[i]) delete _derivedVars[i];
	}
	if (_derivedTime) delete _derivedTime;

	if (_ncdfc) delete _ncdfc;
}


int DCWRF::initialize(
	const vector <string> &files, const std::vector <string> &options
) {

	NetCDFCollection *ncdfc = new NetCDFCollection();

	// Initialize the NetCDFCollection class. Need to specify the name
	// of the time dimension ("Time" for WRF), and time coordinate variable
	// names (N/A for WRF)
	//
	vector <string> time_dimnames;
	vector <string> time_coordvars;
	time_dimnames.push_back("Time");
	int rc = ncdfc->Initialize(files, time_dimnames, time_coordvars);
	if (rc<0) {
		SetErrMsg("Failed to initialize netCDF data collection for reading");
		return(-1);
	}



	// Use UDUnits for unit conversion
	//
	rc = _udunits.Initialize();
	if (rc<0) {
		SetErrMsg(
			"Failed to initialize udunits2 library : %s",
			_udunits.GetErrMsg().c_str()
		);
		return(-1);
	}

	// Get required and optional global attributes  from WRF files.
	// Initializes members: _dx, _dy, _cen_lat, _cen_lon, _pole_lat,
	// _pole_lon, _grav, _radius, _p2si
	//
	rc = _InitAtts(ncdfc);
	if (rc < 0) return(-1);

	//
	//  Get the dimensions of the grid. 
	//	Initializes members: _dimsMap
	//
	rc = _InitDimensions(ncdfc);
	if (rc< 0) {
		SetErrMsg("No valid dimensions");
		return(-1);
	}

	// Set up map projection transforms
	// Initializes members: _proj4String, _mapProj
	//
	rc = _InitProjection(ncdfc, _radius);
	if (rc< 0) {
		return(-1);
	}

	// Set up the horizontal coordinate variables
	//
	// Initializes members: _coordVarMap
	//
	rc = _InitHorizontalCoordinates(ncdfc);
	if (rc<0) {
		return(-1);
	}

	// Set up the vertical coordinate variable. WRF data set doesn't 
	// provide one.
	// 
	// Initializes members: _coordVarMap
	//
	rc = _InitVerticalCoordinates(ncdfc);
	if (rc<0) {
		return(-1);
	}

	// Set up user time coordinate derived variable . Time must be
	// in seconds.
	// Initializes members: _coordVarsMap
	//
	rc = _InitTime(ncdfc);
	if (rc<0) {
		return(-1);
	}

	//
	// Identify data and coordinate variables. Sets up members:
	// Initializes members: _dataVarsMap, _meshMap, _coordVarsMap
	//
	rc = _InitVars(ncdfc) ;
	if (rc<0) return(-1);

	_ncdfc = ncdfc;

	return(0);
}


bool DCWRF::getDimension(
	string dimname, DC::Dimension &dimension
) const {
	map <string, DC::Dimension>::const_iterator itr;

	itr = _dimsMap.find(dimname);
	if (itr == _dimsMap.end()) return(false);

	dimension = itr->second;
	return(true); 
}

std::vector <string> DCWRF::getDimensionNames() const {
	map <string, DC::Dimension>::const_iterator itr;

	vector <string> names;

	for (itr=_dimsMap.begin(); itr != _dimsMap.end(); ++itr) {
		names.push_back(itr->first);
	}

	return(names);
}

vector <string> DCWRF::getMeshNames() const {
	vector <string> mesh_names;
	std::map <string, Mesh>::const_iterator itr = _meshMap.begin();
	for (;itr!=_meshMap.end(); ++itr) {
		mesh_names.push_back(itr->first);
	}
	return(mesh_names);
}

bool DCWRF::getMesh(
	string mesh_name, DC::Mesh &mesh
) const {

	map <string, Mesh>::const_iterator itr = _meshMap.find(mesh_name);
	if (itr == _meshMap.end()) return (false);

	mesh = itr->second;
	return(true);
}

bool DCWRF::getCoordVarInfo(string varname, DC::CoordVar &cvar) const {

	map <string, DC::CoordVar>::const_iterator itr;

	itr = _coordVarsMap.find(varname);
	if (itr == _coordVarsMap.end()) {
		return(false);
	}

	cvar = itr->second;
	return(true);
}

bool DCWRF::getDataVarInfo( string varname, DC::DataVar &datavar) const {

	map <string, DC::DataVar>::const_iterator itr;

	itr = _dataVarsMap.find(varname);
	if (itr == _dataVarsMap.end()) {
		return(false);
	}

	datavar = itr->second;
	return(true);
}

bool DCWRF::getBaseVarInfo(string varname, DC::BaseVar &var) const {
	map <string, DC::CoordVar>::const_iterator itr;

	itr = _coordVarsMap.find(varname);
	if (itr != _coordVarsMap.end()) {
		var = itr->second;
		return(true);
	}

	map <string, DC::DataVar>::const_iterator itr1 = _dataVarsMap.find(varname);
	if (itr1 != _dataVarsMap.end()) {
		var = itr1->second;
		return(true);
	}

	return(false);
}


std::vector <string> DCWRF::getDataVarNames() const {
	map <string, DC::DataVar>::const_iterator itr;

	vector <string> names;
	for (itr = _dataVarsMap.begin(); itr != _dataVarsMap.end(); ++itr) {
		names.push_back(itr->first);
	}
	return(names);
}


std::vector <string> DCWRF::getCoordVarNames() const {
	map <string, DC::CoordVar>::const_iterator itr;

	vector <string> names;
	for (itr = _coordVarsMap.begin(); itr != _coordVarsMap.end(); ++itr) {
		names.push_back(itr->first);
	}
	return(names);
}


template <class T>
bool DCWRF::_getAttTemplate(
	string varname, string attname, T &values
) const {

	DC::BaseVar var;
	bool status = getBaseVarInfo(varname, var);
	if (! status) return(status);

	DC::Attribute att;
	status = var.GetAttribute(attname, att);
	if (! status) return(status);

	att.GetValues(values);

	return(true);
}

bool DCWRF::getAtt(
	string varname, string attname, vector <double> &values
) const {
	values.clear();

	return(_getAttTemplate(varname, attname, values));
}

bool DCWRF::getAtt(
	string varname, string attname, vector <long> &values
) const {
	values.clear();

	return(_getAttTemplate(varname, attname, values));
}

bool DCWRF::getAtt(
	string varname, string attname, string &values
) const {
	values.clear();

	return(_getAttTemplate(varname, attname, values));
}

std::vector <string> DCWRF::getAttNames(string varname) const {
	DC::BaseVar var;
	bool status = getBaseVarInfo(varname, var);
	if (! status) return(vector <string> ());

	vector <string> names;

	const std::map <string, Attribute> &atts = var.GetAttributes();
	std::map <string, Attribute>::const_iterator itr;
	for (itr = atts.begin(); itr!=atts.end(); ++itr) {
		names.push_back(itr->first);
	}

	return(names);
}

DC::XType DCWRF::getAttType(string varname, string attname) const {
	DC::BaseVar var;
	bool status = getBaseVarInfo(varname, var);
	if (! status) return(DC::INVALID);

	DC::Attribute att;
	status = var.GetAttribute(attname, att);
	if (! status) return(DC::INVALID);

	return(att.GetXType());
}
int DCWRF::getDimLensAtLevel(
	string varname, int, std::vector <size_t> &dims_at_level,
	std::vector <size_t> &bs_at_level
) const {
	dims_at_level.clear();
	bs_at_level.clear();

	bool ok;
	if (_dvm.IsCoordVar(varname)) {
		return(_dvm.GetDimLensAtLevel(varname, 0, dims_at_level, bs_at_level));
	}
	else {
		ok = GetVarDimLens(varname, true, dims_at_level);
	}
	if (!ok) {
		SetErrMsg("Undefined variable name : %s", varname.c_str());
		return(-1);
	}

	// Never blocked
	//
	bs_at_level = dims_at_level;

	return(0);
}

string DCWRF::getMapProjection() const {
	return(_proj4String);
}


int DCWRF::openVariableRead(
	size_t ts, string varname
) {

	if (ts >= _ncdfc->GetNumTimeSteps()) {
		SetErrMsg("Time step out of range : %d", ts);
		return(-1);
	}
	ts = _derivedTime->TimeLookup(ts);


	int fd;
	bool derivedFlag;
	if (_dvm.IsCoordVar(varname)) {

		fd = _dvm.OpenVariableRead(ts, varname);
		derivedFlag = true;
	}
	else {
		fd = _ncdfc->OpenRead(ts, varname);
		derivedFlag = false;
	}
	if (fd<0) return(-1);

	WRFFileObject *w = new WRFFileObject(
		ts, varname, 0,0, fd, derivedFlag
	);

	return(_fileTable.AddEntry(w));
}



int DCWRF::closeVariable(int fd) {

	WRFFileObject *w = (WRFFileObject *) _fileTable.GetEntry(fd);

    if (! w) {
        SetErrMsg("Invalid file descriptor : %d", fd);
        return(-1);
    }
	int aux = w->GetAux();

	int rc;
	if (w->GetDerivedFlag()) {
		rc = _dvm.CloseVariable(aux);
	}
	else {
		rc = _ncdfc->Close(aux);
	}
    _fileTable.RemoveEntry(fd);
	delete w;

	return(rc);
}

template <class T>
int DCWRF::_readRegionTemplate(
	int fd,
	const vector <size_t> &min, const vector <size_t> &max, T *region
) {
	assert(min.size() == max.size());

	WRFFileObject *w = (WRFFileObject *) _fileTable.GetEntry(fd);

    if (! w) {
        SetErrMsg("Invalid file descriptor : %d", fd);
        return(-1);
    }
	int aux = w->GetAux();

	if (w->GetDerivedFlag()) {
		return(_dvm.ReadRegion(aux, min, max, region));
	}

	vector <size_t> ncdf_start = min;
	reverse(ncdf_start.begin(), ncdf_start.end());

	vector <size_t> ncdf_max = max;
	reverse(ncdf_max.begin(), ncdf_max.end());

	vector <size_t> ncdf_count;
	for (int i=0; i<ncdf_start.size(); i++) {
		ncdf_count.push_back(ncdf_max[i] - ncdf_start[i] + 1);
	}

    return(_ncdfc->Read(ncdf_start, ncdf_count, region, aux));
}

bool DCWRF::variableExists(
	size_t ts, string varname, int, int 
) const {
    if (ts >= _ncdfc->GetNumTimeSteps()) {
        return(false);
    }
	ts = _derivedTime->TimeLookup(ts);

	if (_dvm.IsCoordVar(varname)) {
		return(_dvm.VariableExists(ts, varname, 0, 0));
	}
	return(_ncdfc->VariableExists(ts, varname));
}


vector <size_t> DCWRF::_GetSpatialDims(
	NetCDFCollection *ncdfc, string varname
) const {
	vector <size_t> dims = ncdfc->GetSpatialDims(varname);
	reverse(dims.begin(), dims.end());
	return(dims);
}

vector <string> DCWRF::_GetSpatialDimNames(
	NetCDFCollection *ncdfc, string varname
) const {
	vector <string> v = ncdfc->GetSpatialDimNames(varname);
	reverse(v.begin(), v.end());
	return(v);
}

//
// Read select attributes from the WRF files. Most of the attributes are
// needed for map projections 
//
int DCWRF::_InitAtts(
	NetCDFCollection *ncdfc
) {

	_dx = -1.0;
	_dy = -1.0;
	_cen_lat = 0.0;
	_cen_lon = 0.0;
	_pole_lat = 90.0;
	_pole_lon = 0.0;
	_grav = 9.81;
	_radius = 0.0;
	_p2si = 1.0;
	

	vector <double> dvalues;
	ncdfc->GetAtt("", "DX", dvalues);
	if (dvalues.size() != 1) {
		SetErrMsg("Error reading required attribute : DX");
		return(-1);
	}
	_dx = dvalues[0];

	ncdfc->GetAtt("", "DY", dvalues);
	if (dvalues.size() != 1) {
		SetErrMsg("Error reading required attribute : DY");
		return(-1);
	}
	_dy = dvalues[0];

	ncdfc->GetAtt("", "CEN_LAT", dvalues);
	if (dvalues.size() != 1) {
		SetErrMsg("Error reading required attribute : CEN_LAT");
		return(-1);
	}
	_cen_lat = dvalues[0];

	ncdfc->GetAtt("", "CEN_LON", dvalues);
	if (dvalues.size() != 1) {
		SetErrMsg("Error reading required attribute : CEN_LON");
		return(-1);
	}
	_cen_lon = dvalues[0];

	ncdfc->GetAtt("", "POLE_LAT", dvalues);
	if (dvalues.size() != 1) _pole_lat = 90.0;
	else _pole_lat = dvalues[0];

	ncdfc->GetAtt("", "POLE_LON", dvalues);
	if (dvalues.size() != 1) _pole_lon = 0.0;
	else _pole_lon = dvalues[0];

	//
	// "PlanetWRF" attributes
	//
	// RADIUS is the radius of the planet 
	//
	// P2SI is the number of SI seconds in an planetary solar day
	// divided by the number of SI seconds in an earth solar day
	//
	ncdfc->GetAtt("", "G", dvalues);
	if (dvalues.size() == 1) {

		_grav = dvalues[0];

		ncdfc->GetAtt("", "RADIUS", dvalues);
		if (dvalues.size() != 1) {
			SetErrMsg("Error reading required attribute : RADIUS");
			return(-1);
		}
		_radius = dvalues[0];

		ncdfc->GetAtt("", "P2SI", dvalues);
		if (dvalues.size() != 1) {
			SetErrMsg("Error reading required attribute : P2SI");
			return(-1);
		}
		_p2si = dvalues[0];
	}

	return(0);
}

//
// Generate a Proj4 projection string for whatever map projection is used
// by the data. Map projection type is indicated by map_proj
// The Proj4 string will be used to transform from geographic coordinates
// measured in degrees to Cartographic coordinates in meters.
//
int DCWRF::_GetProj4String(
	NetCDFCollection *ncdfc, float radius, int map_proj, string &projstring
) {
	projstring.clear();
	ostringstream oss;

	vector <double> dvalues;
	switch (map_proj) {
	case 0 : {//Lat Lon

		double lon_0 = _cen_lon;
		double lat_0 = _cen_lat;
		oss << " +lon_0=" << lon_0 << " +lat_0=" << lat_0;
		projstring = "+proj=eqc +ellps=WGS84" + oss.str();

	}
	break;
	case 1: { //Lambert
		ncdfc->GetAtt("", "STAND_LON", dvalues);
		if (dvalues.size() != 1) {
			SetErrMsg("Error reading required attribute : STAND_LON");
			return(-1);
		}
		float lon0 = dvalues[0];

		ncdfc->GetAtt("", "TRUELAT1", dvalues);
		if (dvalues.size() != 1) {
			SetErrMsg("Error reading required attribute : TRUELAT1");
			return(-1);
		}
		float lat1 = dvalues[0];

		ncdfc->GetAtt("", "TRUELAT2", dvalues);
		if (dvalues.size() != 1) {
			SetErrMsg("Error reading required attribute : TRUELAT2");
			return(-1);
		}
		float lat2 = dvalues[0];
		
		//Construct the projection string:
		projstring = "+proj=lcc";
		projstring += " +lon_0=";
		oss.str("");
		oss << (double)lon0;
		projstring += oss.str();

		projstring += " +lat_1=";
		oss.str("");
		oss << (double)lat1;
		projstring += oss.str();

		projstring += " +lat_2=";
		oss.str("");
		oss << (double)lat2;
		projstring += oss.str();

	break;
	}

	case 2: { //Polar stereographic (pure north or south)
		projstring = "+proj=stere";

		//Determine whether north or south pole (lat_ts is pos or neg)
		
		ncdfc->GetAtt("", "TRUELAT1", dvalues);
		if (dvalues.size() != 1) {
			SetErrMsg("Error reading required attribute : TRUELAT1");
			return(-1);
		}
		float latts = dvalues[0];
	
		float lat0;
		if (latts < 0.) lat0 = -90.0;
		else lat0 = 90.0;

		projstring += " +lat_0=";
		oss.str("");
		oss << (double)lat0;
		projstring += oss.str();

		projstring += " +lat_ts=";
		oss.str("");
		oss << (double)latts;
		projstring += oss.str();

		ncdfc->GetAtt("", "STAND_LON", dvalues);
		if (dvalues.size() != 1) {
			SetErrMsg("Error reading required attribute : STAND_LON");
			return(-1);
		}
		float lon0 = dvalues[0];
		
		projstring += " +lon_0=";
		oss.str("");
		oss << (double)lon0;
		projstring += oss.str();

	break;
	}

	case(3): { //Mercator
		
		ncdfc->GetAtt("", "TRUELAT1", dvalues);
		if (dvalues.size() != 1) {
			SetErrMsg("Error reading required attribute : TRUELAT1");
			return(-1);
		}
		float latts = dvalues[0];

		ncdfc->GetAtt("", "STAND_LON", dvalues);
		if (dvalues.size() != 1) {
			SetErrMsg("Error reading required attribute : STAND_LON");
			return(-1);
		}
		float lon0 = dvalues[0];

		//Construct the projection string:
		projstring = "+proj=merc";

		projstring += " +lon_0=";
		oss.str("");
		oss << (double)lon0;
		projstring += oss.str();
		
		projstring += " +lat_ts=";
		oss.str("");
		oss << (double)latts;
		projstring += oss.str();

	break;
	}

	case(6): { // lat-long, possibly rotated, possibly cassini
		
		// See if this is a regular cylindrical equidistant projection
		// with the pole in the default location
		//
		if (_pole_lat == 90.0 && _pole_lon == 0.0) {

			double lon_0 = _cen_lon;
			double lat_0 = _cen_lat;
			ostringstream oss;
			oss << " +lon_0=" << lon_0 << " +lat_0=" << lat_0;
			projstring = "+proj=eqc +ellps=WGS84" + oss.str();
		}
		else {

			//
			// Assume arbitrary pole displacement. Probably should 
			// check for cassini projection (transverse cylindrical)
			// but general rotated cyl. equidist. projection should work
			//
			ncdfc->GetAtt("", "STAND_LON", dvalues);
			if (dvalues.size() != 1) {
				SetErrMsg("Error reading required attribute : STAND_LON");
				return(-1);
			}
			float lon0 = dvalues[0];

			projstring = "+proj=ob_tran";
			projstring += " +o_proj=eqc";
			projstring += " +to_meter=0.0174532925199";

			projstring += " +o_lat_p=";
			oss.str("");
			oss << (double) _pole_lat;
			projstring += oss.str();
			projstring += "d"; //degrees, not radians

			projstring += " +o_lon_p=";
			oss.str("");
			oss << (double)(180.-_pole_lon);
			projstring += oss.str();
			projstring += "d"; //degrees, not radians

			projstring += " +lon_0=";
			oss.str("");
			oss << (double)(-lon0);
			projstring += oss.str();
			projstring += "d"; //degrees, not radians
		}
		
		break;
	}
	default: {

		SetErrMsg("Unsupported MAP_PROJ value : %d", _mapProj);
		return -1;
	}
	}

	if (projstring.empty()) return(0);

	//
	// PlanetWRF data if radius is not zero
	//
	if (radius > 0) {	// planetWRf (not on earth)
		projstring += " +ellps=sphere";
		stringstream ss;
		ss << radius;
		projstring += " +a=" + ss.str() + " +es=0";
	}
	else {
		projstring += " +ellps=WGS84";
	}

	return(0);
}

//
// Set up map projection stuff
//
int DCWRF::_InitProjection(
	NetCDFCollection *ncdfc, float radius
) {
	_proj4String.clear();
	_mapProj = 0;


	vector <long> ivalues;
	ncdfc->GetAtt("", "MAP_PROJ", ivalues);
	if (ivalues.size() != 1) {
		SetErrMsg("Error reading required attribute : MAP_PROJ");
		return(-1);
	}
	_mapProj = ivalues[0];

	int rc = _GetProj4String(ncdfc, radius, _mapProj, _proj4String);
	if (rc<0) return(rc);


	return(0);
}

DerivedCoordVar_Staggered * DCWRF::_makeDerivedHorizontal(
	NetCDFCollection *ncdfc, string name, 
	string &timeDimName, vector <string> &spaceDimNames
) {
	timeDimName.clear();
	spaceDimNames.clear();

	int stagDim;
	string stagDimName;
	string inName;
	string dimName;
	if (name == "XLONG_U") {
		stagDim = 0;
		stagDimName = "west_east_stag";
		inName = "XLONG";
		dimName = "west_east";
	}
	else if (name == "XLAT_U") {
		stagDim = 0;
		stagDimName = "west_east_stag";
		inName = "XLAT";
		dimName = "west_east";
	}
	else if (name == "XLONG_V") {
		stagDim = 1;
		stagDimName = "south_north_stag";
		inName = "XLONG";
		dimName = "south_north";
	}
	else if (name == "XLAT_V") {
		stagDim = 1;
		stagDimName = "south_north_stag";
		inName = "XLAT";
		dimName = "south_north";
	}
	else {
		return(NULL);
	}

	timeDimName = ncdfc->GetTimeDimName(inName);
	spaceDimNames = ncdfc->GetSpatialDimNames(inName);
	reverse(spaceDimNames.begin(), spaceDimNames.end());

	spaceDimNames[stagDim] = stagDimName;

	DerivedCoordVar_Staggered *derivedVar = new DerivedCoordVar_Staggered(
		name, stagDimName, this, inName, dimName
	);
	int rc = derivedVar->Initialize();
	if (rc < 0) return(NULL);

	_dvm.AddCoordVar(derivedVar);

	return(derivedVar);
		
}

int DCWRF::_InitHorizontalCoordinatesHelper(
	NetCDFCollection *ncdfc, string name, int axis
) {
	assert(axis == 0 || axis == 1);

	DerivedCoordVar_Staggered *derivedVar = NULL;

	string timeDimName;
	vector <string> spaceDimNames;

	// Ugh. Older WRF files don't have coordinate variables for 
	// staggered dimensions, so we need to derive them.
	//
	if (ncdfc->VariableExists(name)) {

		timeDimName = ncdfc->GetTimeDimName(name);

		spaceDimNames = ncdfc->GetSpatialDimNames(name);
		reverse(spaceDimNames.begin(), spaceDimNames.end());
	}
	else {
		derivedVar = _makeDerivedHorizontal(
			ncdfc, name, timeDimName, spaceDimNames
		);
		if (! derivedVar) return(-1);
		
	}

	string units = axis == 0 ? "degrees_east" : "degrees_north";

	// Finally, add the variable to _coordVarsMap. Probably don't 
	// need to do this here. Could do this when we process native WRF
	// variables later. Sigh
	//
	vector <bool> periodic(2, false);
	_coordVarsMap[name] = CoordVar(
		name, units, DC::FLOAT, periodic, axis, false, 
		spaceDimNames, timeDimName
	);

	int rc = DCUtils::CopyAtt(*ncdfc, name, _coordVarsMap[name]);
	if (rc<0) return(-1);

	if (derivedVar) {
		_derivedVars.push_back(derivedVar);
	}

	return (0);
}

//
// Set up horizontal coordinates
//
int DCWRF::_InitHorizontalCoordinates(
	NetCDFCollection *ncdfc
) {
	_coordVarsMap.clear();

	// XLONG and XLAT must have same dimensionality
	//
	vector <size_t> latlondims = ncdfc->GetDims("XLONG");
	vector <size_t> dummy = ncdfc->GetDims("XLAT");
	if (latlondims.size() != 3 || dummy != latlondims) {
		SetErrMsg("Invalid coordinate variable : %s", "XLONG");
		return(-1);
	}

	// "XLONG" coordinate, unstaggered
	//
	(void) _InitHorizontalCoordinatesHelper(ncdfc, "XLONG", 0);

	// "XLAT" coordinate, unstaggered
	//
	(void) _InitHorizontalCoordinatesHelper(ncdfc, "XLAT", 1);

	// "XLONG_U" coordinate, staggered
	//
	(void) _InitHorizontalCoordinatesHelper(ncdfc, "XLONG_U", 0);

	// "XLAT_U" coordinate, staggered
	//
	(void) _InitHorizontalCoordinatesHelper(ncdfc, "XLAT_U", 1);

	// "XLONG_V" coordinate, staggered
	//
	(void) _InitHorizontalCoordinatesHelper(ncdfc, "XLONG_V", 0);

	// "XLAT_V" coordinate, staggered
	//
	(void) _InitHorizontalCoordinatesHelper(ncdfc, "XLAT_V", 1);

	return(0);
}

DerivedCoordVar_CF1D *DCWRF::_InitVerticalCoordinatesHelper(
	string varName, string dimName
) {

	DerivedCoordVar_CF1D *derivedVar;

	vector <string> varNames = {varName};
	vector <string> dimNames = {dimName};
	string units = "";
	int axis = 2;

	
	derivedVar = new DerivedCoordVar_CF1D(
		varNames, this, dimName, axis, units
	);
	(void) derivedVar->Initialize();

	_dvm.AddCoordVar(derivedVar);

	vector <bool> periodic(1, false);
	string time_dim_name = "";

	_coordVarsMap[varName] = CoordVar(
		varName, units, DC::FLOAT, periodic, axis, false,
		dimNames, time_dim_name
	);

	return(derivedVar);
}

//
// Create 1D derived variables expressing the vertical coordinates
// in unitless grid index coordinates.
//
int DCWRF::_InitVerticalCoordinates(
	NetCDFCollection *ncdfc
) {

	// Create 1D vertical coordinate variable for each "vertical" dimension
	//
	// N.B. This only deals with the vertical dimensions we know about.
	// Could be others.
	//
	string name = "bottom_top";
	if (_dimsMap.find(name) != _dimsMap.end()) {
		_derivedVars.push_back(_InitVerticalCoordinatesHelper(name, name));
	}

	name = "bottom_top_stag";
	if (_dimsMap.find(name) != _dimsMap.end()) {
		_derivedVars.push_back(_InitVerticalCoordinatesHelper(name, name));
	}

	name = "soil_layers_stag";
	if (_dimsMap.find(name) != _dimsMap.end()) {
		_derivedVars.push_back(_InitVerticalCoordinatesHelper(name, name));
	}


	return(0);
}


// Create a derived variable for the time coordinate. Time in WRF data
// is an array of formatted time strings. The DC class requires that
// time be expressed as seconds represented as floats.
//
int DCWRF::_InitTime(
	NetCDFCollection *ncdfc
) {
	_derivedTime = NULL;

	// Create and install the Time coordinate variable
	//
	
	string derivedName = "Time";
	string wrfVarName = "Times";
	string dimName = "Time";
	_derivedTime = new DerivedCoordVar_WRFTime(
		derivedName, ncdfc, wrfVarName, dimName, _p2si
	);

	int rc = _derivedTime->Initialize();
	if (rc<0) return(-1);

	_dvm.AddCoordVar(_derivedTime);

	DC::CoordVar cvarInfo;
	(void) _dvm.GetCoordVarInfo(derivedName, cvarInfo);
	
	_coordVarsMap[derivedName] = cvarInfo;

	return(0);
}

// Get Space and time dimensions from WRF data set. Initialize
// _dimsMap 
//
int DCWRF::_InitDimensions(
	NetCDFCollection *ncdfc
) {
	_dimsMap.clear();

	// Get dimension names and lengths for all dimensions in the
	// WRF data set. 
	//
	vector <string> dimnames = ncdfc->GetDimNames();
	vector <size_t> dimlens = ncdfc->GetDims();
	assert(dimnames.size() == dimlens.size());

	// WRF files use reserved names for dimensions. The time dimension
	// is always named "Time", etc.
	// Dimensions are expressed in the DC::Dimension class as a
	// combination of name, and length.
	//
	string timedimname = "Time";
	for (int i=0; i<dimnames.size(); i++) {

		Dimension dim(dimnames[i], dimlens[i]);
		_dimsMap[dimnames[i]] = dim;
	}

	if (
		(_dimsMap.find("west_east") == _dimsMap.end()) ||
		(_dimsMap.find("west_east_stag") == _dimsMap.end()) ||
		(_dimsMap.find("south_north") == _dimsMap.end()) ||
		(_dimsMap.find("south_north_stag") == _dimsMap.end()) ||
		// (_dimsMap.find("bottom_top") == _dimsMap.end()) ||
		// (_dimsMap.find("bottom_top_stag") == _dimsMap.end()) ||
		(_dimsMap.find("Time") == _dimsMap.end())) {

		SetErrMsg("Missing dimension");
		return(-1);
	}
	return(0);
}


// Given a data variable name return the variable's dimension names and
// associated coordinate variables. The coordinate variable names
// returned is for the derived coordinate variables expressed in 
// Cartographic coordinates, not the native geographic coordinates
// found in the WRF file. 
//
// The order of the returned vectors
// is significant.
//
bool DCWRF::_GetVarCoordinates(
	NetCDFCollection *ncdfc, string varname,
	vector <string> &sdimnames,
	vector <string> &scoordvars,
	string &time_dim_name,
	string &time_coordvar
	
) {
	sdimnames.clear();
	scoordvars.clear();
	time_dim_name.clear();
	time_coordvar.clear();

	// Order of dimensions in WRF files is reverse of DC convention
	//
	vector <string> dimnames = ncdfc->GetDimNames(varname);
	reverse(dimnames.begin(), dimnames.end());

	// Deal with time dimension first
	//
	if (dimnames.size() == 1) {
		if (dimnames[0].compare("Time")!=0) {
			return(false);
		}
		time_dim_name = "Time";
		time_coordvar = "Time";
		return(true);
	} 

	// only handle 2d, 3d, and 4d variables
	//
	if (dimnames.size() < 2) return(false);


	if (
		dimnames[0].compare("west_east")==0 && 
		dimnames[1].compare("south_north")==0
	) {
		scoordvars.push_back("XLONG");
		scoordvars.push_back("XLAT");
	}
	else if (
		dimnames[0].compare("west_east_stag")==0 && 
		dimnames[1].compare("south_north")==0
	) {
		scoordvars.push_back("XLONG_U");
		scoordvars.push_back("XLAT_U");
	}
	else if (
		dimnames[0].compare("west_east")==0 && 
		dimnames[1].compare("south_north_stag")==0
	) {
		scoordvars.push_back("XLONG_V");
		scoordvars.push_back("XLAT_V");
	}
	else {
		return(false);
	}

	if (dimnames.size() > 2 && dimnames[2] != "Time") {
		scoordvars.push_back(dimnames[2]);
	}

	sdimnames = dimnames;

	if (dimnames.size()==2) {
		return(true);
	}

	if (sdimnames.back().compare("Time")==0) {
		time_dim_name = "Time";
		time_coordvar = "Time";
		sdimnames.pop_back();	// Oops. Remove time dimension
	}
	return(true);

}

// Collect metadata for all data variables found in the WRF data 
// set. Initialize the _dataVarsMap member
//
int DCWRF::_InitVars(NetCDFCollection *ncdfc) 
{
	_dataVarsMap.clear();
	_meshMap.clear();

	//
	// Get names of variables  in the WRF data set that have 1 2 or 3
	// spatial dimensions
	//
	vector <string> vars;
	for (int i=1; i<4; i++) {
		vector <string> v = ncdfc->GetVariableNames(i,true);
		vars.insert(vars.end(), v.begin(), v.end());
	}

	// For each variable add a member to _dataVarsMap
	//
	for (int i=0; i<vars.size(); i++) {

		// variable type must be float or int
		//
		int type = ncdfc->GetXType(vars[i]);
		if ( ! (
			NetCDFSimple::IsNCTypeFloat(type) || 
			NetCDFSimple::IsNCTypeFloat(type))) continue; 

		// If variables are in _coordVarsMap then they are coordinate, not
		// data, variables
		//
		if (_coordVarsMap.find(vars[i]) != _coordVarsMap.end()) continue; 
		
		vector <string> sdimnames;
		vector <string> scoordvars;
		string time_dim_name;
		string time_coordvar;
		

		bool ok = _GetVarCoordinates(
			ncdfc, vars[i], sdimnames, scoordvars, time_dim_name, time_coordvar
		);

		// Must have a coordinate variable for each dimension!
		//
		if (sdimnames.size() != scoordvars.size()) {
cout << "CRAP\n";
			continue;
		}

		if (! ok) continue;
		//if (! ok) {
		//	SetErrMsg("Invalid variable : %s", vars[i].c_str());
		//	return(-1);
		//}

		Mesh mesh("", sdimnames, scoordvars);

		// Create new mesh. We're being lazy here and probably should only
		// createone if it doesn't ready exist
		//
		_meshMap[mesh.GetName()] = mesh;

		string units;
		ncdfc->GetAtt(vars[i], "units", units);
		if (! _udunits.ValidUnit(units)) {
			units = "";
		} 

		vector <bool> periodic(3, false);
		_dataVarsMap[vars[i]] = DataVar(
			vars[i], units, DC::FLOAT, periodic, mesh.GetName(),
			time_coordvar, DC::Mesh::NODE
		);

		int rc = DCUtils::CopyAtt(*ncdfc, vars[i], _dataVarsMap[vars[i]]);
		if (rc<0) return(-1);
	}

	return(0);
}




