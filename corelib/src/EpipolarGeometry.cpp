/*
Copyright (c) 2010-2014, Mathieu Labbe - IntRoLab - Universite de Sherbrooke
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Universite de Sherbrooke nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "rtabmap/core/EpipolarGeometry.h"
#include "rtabmap/core/Signature.h"
#include "rtabmap/utilite/ULogger.h"
#include "rtabmap/utilite/UTimer.h"
#include "rtabmap/utilite/UStl.h"
#include "rtabmap/utilite/UMath.h"

#include <opencv2/core/core.hpp>
#include <opencv2/core/core_c.h>
#include <opencv2/calib3d/calib3d.hpp>
#include <iostream>

namespace rtabmap
{

/////////////////////////
// HypVerificatorEpipolarGeo
/////////////////////////
EpipolarGeometry::EpipolarGeometry(const ParametersMap & parameters) :
	_matchCountMinAccepted(Parameters::defaultVhEpMatchCountMin()),
	_ransacParam1(Parameters::defaultVhEpRansacParam1()),
	_ransacParam2(Parameters::defaultVhEpRansacParam2())
{
	this->parseParameters(parameters);
}

EpipolarGeometry::~EpipolarGeometry() {

}

void EpipolarGeometry::parseParameters(const ParametersMap & parameters)
{
	Parameters::parse(parameters, Parameters::kVhEpMatchCountMin(), _matchCountMinAccepted);
	Parameters::parse(parameters, Parameters::kVhEpRansacParam1(), _ransacParam1);
	Parameters::parse(parameters, Parameters::kVhEpRansacParam2(), _ransacParam2);
}

bool EpipolarGeometry::check(const Signature * ssA, const Signature * ssB)
{
	if(ssA == 0 || ssB == 0)
	{
		return false;
	}
	ULOGGER_DEBUG("id(%d,%d)", ssA->id(), ssB->id());

	std::list<std::pair<int, std::pair<cv::KeyPoint, cv::KeyPoint> > > pairs;

	findPairsUnique(ssA->getWords(), ssB->getWords(), pairs);

	if((int)pairs.size()<_matchCountMinAccepted)
	{
		return false;
	}

	std::vector<uchar> status;
	cv::Mat f = findFFromWords(pairs, status, _ransacParam1, _ransacParam2);

	int inliers = uSum(status);
	if(inliers < _matchCountMinAccepted)
	{
		ULOGGER_DEBUG("Epipolar constraint failed A : not enough inliers (%d/%d), min is %d", inliers, pairs.size(), _matchCountMinAccepted);
		return false;
	}
	else
	{
		UDEBUG("inliers = %d/%d", inliers, pairs.size());
		return true;
	}
}

//STATIC STUFF
//Epipolar geometry
void EpipolarGeometry::findEpipolesFromF(const cv::Mat & fundamentalMatrix, cv::Vec3d & e1, cv::Vec3d & e2)
{
	if(fundamentalMatrix.rows != 3 || fundamentalMatrix.cols != 3)
	{
		ULOGGER_ERROR("The matrix is not the good size...");
		return;
	}

	if(fundamentalMatrix.type() != CV_64FC1)
	{
		ULOGGER_ERROR("The matrix is not the good type...");
		return;
	}


	cv::SVD svd(fundamentalMatrix);
	cv::Mat u = svd.u;
	cv::Mat v = svd.vt;
	cv::Mat w = svd.w;

	// v is for image 1
	// u is for image 2

	e1[0] = v.at<double>(0,2);// /v->data.db[2*3+2];
	e1[1] = v.at<double>(1,2);// /v->data.db[2*3+2];
	e1[2] = v.at<double>(2,2);// /v->data.db[2*3+2];

	e2[0] = u.at<double>(0,2);// /u->data.db[2*3+2];
	e2[1] = u.at<double>(1,2);// /u->data.db[2*3+2];
	e2[2] = u.at<double>(2,2);// /u->data.db[2*3+2];
}

//Assuming P0 = [eye(3) zeros(3,1)]
// x1 and x2 are 2D points
// return camera matrix P (3x4) matrix
cv::Mat EpipolarGeometry::findPFromF(const cv::Mat & fundamentalMatrix, const cv::Mat & x1, const cv::Mat & x2)
{

	if(fundamentalMatrix.rows != 3 || fundamentalMatrix.cols != 3)
	{
		ULOGGER_ERROR("Matrices are not the good size... ");
		return cv::Mat();
	}

	if(fundamentalMatrix.type() != CV_64FC1)
	{
		ULOGGER_ERROR("Matrices are not the good type...");
		return cv::Mat();
	}

	// P matrix 3x4
	cv::Mat p = cv::Mat::zeros(3, 4, CV_64FC1);

	// P0 matrix 3X4
	cv::Mat p0 = cv::Mat::zeros(3, 4, CV_64FC1);
	p0.at<double>(0,0) = 1;
	p0.at<double>(1,1) = 1;
	p0.at<double>(2,2) = 1;

	// cv::SVD doesn't five same results as cvSVD ?!? cvSVD return same values as in MatLab
	/*cv::SVD svd(fundamentalMatrix);
	cv::Mat u = svd.u;
	cv::Mat v = svd.vt;
	cv::Mat s = svd.w;
	cv::Mat e = u.col(2);*/

	CvMat F  = fundamentalMatrix;
	cv::Mat u(3,3,CV_64F);
	cv::Mat v(3,3,CV_64F);
	cv::Mat s(3,3,CV_64F);
	CvMat U  = u;
	CvMat S  = s;
	CvMat V  = v;
	cvSVD(&F, &S, &U, &V, CV_SVD_U_T|CV_SVD_V_T); // F = U D V^T
	u = u.t();
	//
	// INFO: may be required to multiply by -1 the last column of U
	// TODO: Is any way to detect when it is required to do that ? When
	//       it is wrong, triangulated points have their Z value below 1 (between 0 and 1)...
	//
	/*u.at<double>(0,2) = -u.at<double>(0,2);
	u.at<double>(1,2) = -u.at<double>(1,2);
	u.at<double>(2,2) = -u.at<double>(2,2);*/
	v = v.t();
	cv::Mat e = u.col(2);

	//std::cout << "u=" << u << std::endl;
	//std::cout << "v=" << v << std::endl;
	//std::cout << "s=" << s << std::endl;

	// skew matrix 3X3
	cv::Mat skew = cv::Mat::zeros( 3, 3, CV_64FC1);
	skew.at<double>(0,1) = -1;
	skew.at<double>(1,0) = 1;
	skew.at<double>(2,2) = 1;

	cv::Mat r;
	cv::Mat x4d;

	cv::Mat x = x1.col(0); // just take one point
	cv::Mat xp = x2.col(0); // just take one point

	// INFO: There 4 cases of P, only one have the points in
	// front of the two cameras (positive z).

	// Case 1 : P = [U*W*V' e];
	r = u*skew*v.t();
	p.at<double>(0,0) = r.at<double>(0,0);
	p.at<double>(0,1) = r.at<double>(0,1);
	p.at<double>(0,2) = r.at<double>(0,2);
	p.at<double>(1,0) = r.at<double>(1,0);
	p.at<double>(1,1) = r.at<double>(1,1);
	p.at<double>(1,2) = r.at<double>(1,2);
	p.at<double>(2,0) = r.at<double>(2,0);
	p.at<double>(2,1) = r.at<double>(2,1);
	p.at<double>(2,2) = r.at<double>(2,2);
	p.at<double>(0,3) = e.at<double>(0,0);
	p.at<double>(1,3) = e.at<double>(1,0);
	p.at<double>(2,3) = e.at<double>(2,0);

	cv::triangulatePoints(p0, p, x, xp, x4d);
	x4d.at<double>(0) = x4d.at<double>(0)/x4d.at<double>(3);
	x4d.at<double>(1) = x4d.at<double>(1)/x4d.at<double>(3);
	x4d.at<double>(2) = x4d.at<double>(2)/x4d.at<double>(3);
	x4d.at<double>(3) = x4d.at<double>(3)/x4d.at<double>(3);

	cv::Mat xt1 = p0*x4d;
	cv::Mat xt2 = p*x4d;

	if(xt1.at<double>(2,0) < 0 || xt2.at<double>(2,0) < 0)
	{
		// Case 2 : P = [U*W*V' -e];
		p.at<double>(0,3) = -e.at<double>(0,0);
		p.at<double>(1,3) = -e.at<double>(1,0);
		p.at<double>(2,3) = -e.at<double>(2,0);
		cv::triangulatePoints(p0, p, x, xp, x4d);
		x4d.at<double>(0) = x4d.at<double>(0)/x4d.at<double>(3);
		x4d.at<double>(1) = x4d.at<double>(1)/x4d.at<double>(3);
		x4d.at<double>(2) = x4d.at<double>(2)/x4d.at<double>(3);
		x4d.at<double>(3) = x4d.at<double>(3)/x4d.at<double>(3);
		xt1 = p0*x4d;
		xt2 = p*x4d;
		if(xt1.at<double>(2,0) < 0 || xt2.at<double>(2,0) < 0)
		{
			// Case 3 : P = [U*W'*V' e];
			r = u*skew.t()*v.t();
			p.at<double>(0,0) = r.at<double>(0,0);
			p.at<double>(0,1) = r.at<double>(0,1);
			p.at<double>(0,2) = r.at<double>(0,2);
			p.at<double>(1,0) = r.at<double>(1,0);
			p.at<double>(1,1) = r.at<double>(1,1);
			p.at<double>(1,2) = r.at<double>(1,2);
			p.at<double>(2,0) = r.at<double>(2,0);
			p.at<double>(2,1) = r.at<double>(2,1);
			p.at<double>(2,2) = r.at<double>(2,2);
			p.at<double>(0,3) = e.at<double>(0,0);
			p.at<double>(1,3) = e.at<double>(1,0);
			p.at<double>(2,3) = e.at<double>(2,0);
			p.col(3) = e;
			cv::triangulatePoints(p0, p, x, xp, x4d);
			x4d.at<double>(0) = x4d.at<double>(0)/x4d.at<double>(3);
			x4d.at<double>(1) = x4d.at<double>(1)/x4d.at<double>(3);
			x4d.at<double>(2) = x4d.at<double>(2)/x4d.at<double>(3);
			x4d.at<double>(3) = x4d.at<double>(3)/x4d.at<double>(3);
			xt1 = p0*x4d;
			xt2 = p*x4d;
			if(xt1.at<double>(2,0) < 0 || xt2.at<double>(2,0) < 0)
			{
				// Case 4 : P = [U*W'*V' -e];
				p.at<double>(0,3) = -e.at<double>(0,0);
				p.at<double>(1,3) = -e.at<double>(1,0);
				p.at<double>(2,3) = -e.at<double>(2,0);
				cv::triangulatePoints(p0, p, x, xp, x4d);
				x4d.at<double>(0) = x4d.at<double>(0)/x4d.at<double>(3);
				x4d.at<double>(1) = x4d.at<double>(1)/x4d.at<double>(3);
				x4d.at<double>(2) = x4d.at<double>(2)/x4d.at<double>(3);
				x4d.at<double>(3) = x4d.at<double>(3)/x4d.at<double>(3);
				xt1 = p0*x4d;
				xt2 = p*x4d;
				UDEBUG("Case 4");
			}
			else
			{
				UDEBUG("Case 3");
			}
		}
		else
		{
			UDEBUG("Case 2");
		}
	}
	else
	{
		UDEBUG("Case 1");
	}
	return p;
}

cv::Mat EpipolarGeometry::findFFromWords(
		const std::list<std::pair<int, std::pair<cv::KeyPoint, cv::KeyPoint> > > & pairs, // id, kpt1, kpt2
		std::vector<uchar> & status,
		double ransacParam1,
		double ransacParam2)
{

	status = std::vector<uchar>(pairs.size(), 0);
	//Convert Keypoints to a structure that OpenCV understands
	//3 dimensions (Homogeneous vectors)
	cv::Mat points1(1, pairs.size(), CV_32FC2);
	cv::Mat points2(1, pairs.size(), CV_32FC2);

	float * points1data = points1.ptr<float>(0);
	float * points2data = points2.ptr<float>(0);

	// Fill the points here ...
	int i=0;
	for(std::list<std::pair<int, std::pair<cv::KeyPoint, cv::KeyPoint> > >::const_iterator iter = pairs.begin();
		iter != pairs.end();
		++iter )
	{
		points1data[i*2] = (*iter).second.first.pt.x;
		points1data[i*2+1] = (*iter).second.first.pt.y;

		points2data[i*2] = (*iter).second.second.pt.x;
		points2data[i*2+1] = (*iter).second.second.pt.y;

		++i;
	}

	UTimer timer;
	timer.start();

	// Find the fundamental matrix
	cv::Mat fundamentalMatrix = cv::findFundamentalMat(
				points1,
				points2,
				status,
				cv::FM_RANSAC,
				ransacParam1,
				ransacParam2);

	ULOGGER_DEBUG("Find fundamental matrix (OpenCV) time = %fs", timer.ticks());

		// Fundamental matrix is valid ?
	bool fundMatFound = false;
	UASSERT(fundamentalMatrix.type() == CV_64FC1);
	if(fundamentalMatrix.cols==3 && fundamentalMatrix.rows==3 &&
	   (fundamentalMatrix.at<double>(0,0) != 0.0 ||
	    fundamentalMatrix.at<double>(0,1) != 0.0 ||
	    fundamentalMatrix.at<double>(0,2) != 0.0 ||
	    fundamentalMatrix.at<double>(1,0) != 0.0 ||
	    fundamentalMatrix.at<double>(1,1) != 0.0 ||
	    fundamentalMatrix.at<double>(1,2) != 0.0 ||
		fundamentalMatrix.at<double>(2,0) != 0.0 ||
		fundamentalMatrix.at<double>(2,1) != 0.0 ||
		fundamentalMatrix.at<double>(2,2) != 0.0) )

	{
		fundMatFound = true;
	}

	ULOGGER_DEBUG("fm_count=%d...", fundMatFound);

	if(fundMatFound)
	{
		// Show the fundamental matrix
		UDEBUG(
			"F = [%f %f %f;%f %f %f;%f %f %f]",
			fundamentalMatrix.ptr<double>(0)[0],
			fundamentalMatrix.ptr<double>(0)[1],
			fundamentalMatrix.ptr<double>(0)[2],
			fundamentalMatrix.ptr<double>(0)[3],
			fundamentalMatrix.ptr<double>(0)[4],
			fundamentalMatrix.ptr<double>(0)[5],
			fundamentalMatrix.ptr<double>(0)[6],
			fundamentalMatrix.ptr<double>(0)[7],
			fundamentalMatrix.ptr<double>(0)[8]);
	}
	return fundamentalMatrix;
}

void EpipolarGeometry::findRTFromP(
		const cv::Mat & p,
		cv::Mat & r,
		cv::Mat & t)
{
	UASSERT(p.cols == 4 && p.rows == 3);
	UDEBUG("");
	r = cv::Mat(p, cv::Range(0,3), cv::Range(0,3));
	UDEBUG("");
	r = -r.inv();
	UDEBUG("r=%d %d, t=%d", r.cols, r.rows, p.col(3).rows);
	t = r*p.col(3);
	UDEBUG("");
}

cv::Mat EpipolarGeometry::findFFromCalibratedStereoCameras(double fx, double fy, double cx, double cy, double Tx, double Ty)
{
	cv::Mat R = cv::Mat::eye(3, 3, CV_64FC1);

	double Bx = Tx/-fx;
	double By = Ty/-fy;

	cv::Mat tx = (cv::Mat_<double>(3,3) <<
			0, 0, By,
			0, 0, -Bx,
			-By, Bx, 0);

	cv::Mat K = (cv::Mat_<double>(3,3) <<
			fx, 0, cx,
			0, fy, cy,
			0, 0, 1);

	cv::Mat E = tx*R;

	return K.inv().t()*E*K.inv();
}

/**
 * if a=[1 2 3 4 6 6], b=[1 1 2 4 5 6 6], results= [(1,1a) (2,2) (4,4) (6a,6a) (6b,6b)]
 * realPairsCount = 5
 */
int EpipolarGeometry::findPairs(const std::multimap<int, cv::KeyPoint> & wordsA,
		const std::multimap<int, cv::KeyPoint> & wordsB,
		std::list<std::pair<int, std::pair<cv::KeyPoint, cv::KeyPoint> > > & pairs)
{
	const std::list<int> & ids = uUniqueKeys(wordsA);
	std::multimap<int, cv::KeyPoint>::const_iterator iterA;
	std::multimap<int, cv::KeyPoint>::const_iterator iterB;
	pairs.clear();
	int realPairsCount = 0;
	for(std::list<int>::const_iterator i=ids.begin(); i!=ids.end(); ++i)
	{
		iterA = wordsA.find(*i);
		iterB = wordsB.find(*i);
		while(iterA != wordsA.end() && iterB != wordsB.end() && (*iterA).first == (*iterB).first && (*iterA).first == *i)
		{
			pairs.push_back(std::pair<int, std::pair<cv::KeyPoint, cv::KeyPoint> >(*i, std::pair<cv::KeyPoint, cv::KeyPoint>((*iterA).second, (*iterB).second)));
			++iterA;
			++iterB;
			++realPairsCount;
		}
	}
	return realPairsCount;
}

/**
 * if a=[1 2 3 4 6 6], b=[1 1 2 4 5 6 6], results= [(2,2) (4,4)]
 * realPairsCount = 5
 */
int EpipolarGeometry::findPairsUnique(
		const std::multimap<int, cv::KeyPoint> & wordsA,
		const std::multimap<int, cv::KeyPoint> & wordsB,
		std::list<std::pair<int, std::pair<cv::KeyPoint, cv::KeyPoint> > > & pairs)
{
	const std::list<int> & ids = uUniqueKeys(wordsA);
	int realPairsCount = 0;
	pairs.clear();
	for(std::list<int>::const_iterator i=ids.begin(); i!=ids.end(); ++i)
	{
		std::list<cv::KeyPoint> ptsA = uValues(wordsA, *i);
		std::list<cv::KeyPoint> ptsB = uValues(wordsB, *i);
		if(ptsA.size() == 1 && ptsB.size() == 1)
		{
			pairs.push_back(std::pair<int, std::pair<cv::KeyPoint, cv::KeyPoint> >(*i, std::pair<cv::KeyPoint, cv::KeyPoint>(ptsA.front(), ptsB.front())));
			++realPairsCount;
		}
		else if(ptsA.size()>1 && ptsB.size()>1)
		{
			// just update the count
			realPairsCount += ptsA.size() > ptsB.size() ? ptsB.size() : ptsA.size();
		}
	}
	return realPairsCount;
}

/**
 * if a=[1 2 3 4 6 6], b=[1 1 2 4 5 6 6], results= [(1,1a) (1,1b) (2,2) (4,4) (6a,6a) (6a,6b) (6b,6a) (6b,6b)]
 * realPairsCount = 5
 */
int EpipolarGeometry::findPairsAll(const std::multimap<int, cv::KeyPoint> & wordsA,
		const std::multimap<int, cv::KeyPoint> & wordsB,
		std::list<std::pair<int, std::pair<cv::KeyPoint, cv::KeyPoint> > > & pairs)
{
	UTimer timer;
	timer.start();
	const std::list<int> & ids = uUniqueKeys(wordsA);
	pairs.clear();
	int realPairsCount = 0;;
	for(std::list<int>::const_iterator iter=ids.begin(); iter!=ids.end(); ++iter)
	{
		std::list<cv::KeyPoint> ptsA = uValues(wordsA, *iter);
		std::list<cv::KeyPoint> ptsB = uValues(wordsB, *iter);

		realPairsCount += ptsA.size() > ptsB.size() ? ptsB.size() : ptsA.size();

		for(std::list<cv::KeyPoint>::iterator jter=ptsA.begin(); jter!=ptsA.end(); ++jter)
		{
			for(std::list<cv::KeyPoint>::iterator kter=ptsB.begin(); kter!=ptsB.end(); ++kter)
			{
				pairs.push_back(std::pair<int, std::pair<cv::KeyPoint, cv::KeyPoint> >(*iter, std::pair<cv::KeyPoint, cv::KeyPoint>(*jter, *kter)));
			}
		}
	}
	ULOGGER_DEBUG("time = %f", timer.ticks());
	return realPairsCount;
}



/**
 source = SfM toy library: https://github.com/royshil/SfM-Toy-Library
 From "Triangulation", Hartley, R.I. and Sturm, P., Computer vision and image understanding, 1997
 */
cv::Mat EpipolarGeometry::linearLSTriangulation(
		cv::Point3d u,   //homogenous image point (u,v,1)
		cv::Matx34d P,       //camera 1 matrix 3x4 double
		cv::Point3d u1,  //homogenous image point in 2nd camera
		cv::Matx34d P1       //camera 2 matrix 3x4 double
                                   )
{
    //build matrix A for homogenous equation system Ax = 0
    //assume X = (x,y,z,1), for Linear-LS method
    //which turns it into a AX = B system, where A is 4x3, X is 3x1 and B is 4x1
    cv::Mat A = (cv::Mat_<double>(4,3) <<
    		u.x*P(2,0)-P(0,0),    u.x*P(2,1)-P(0,1),      u.x*P(2,2)-P(0,2),
			u.y*P(2,0)-P(1,0),    u.y*P(2,1)-P(1,1),      u.y*P(2,2)-P(1,2),
			u1.x*P1(2,0)-P1(0,0), u1.x*P1(2,1)-P1(0,1),   u1.x*P1(2,2)-P1(0,2),
			u1.y*P1(2,0)-P1(1,0), u1.y*P1(2,1)-P1(1,1),   u1.y*P1(2,2)-P1(1,2)
              );
    cv::Mat B = (cv::Mat_<double>(4,1) <<
			-(u.x*P(2,3)    -P(0,3)),
			-(u.y*P(2,3)  -P(1,3)),
			-(u1.x*P1(2,3)    -P1(0,3)),
			-(u1.y*P1(2,3)    -P1(1,3)));

    cv::Mat X;
    solve(A,B,X,cv::DECOMP_SVD);

    return X;
}

 /**
 source = SfM toy library: https://github.com/royshil/SfM-Toy-Library
 From "Triangulation", Hartley, R.I. and Sturm, P., Computer vision and image understanding, 1997
 */
cv::Mat EpipolarGeometry::iterativeLinearLSTriangulation(
		cv::Point3d u,            //homogenous image point (u,v,1)
		const cv::Matx34d & P,   //camera 1 matrix 3x4 double
		cv::Point3d u1,           //homogenous image point in 2nd camera
		const cv::Matx34d & P1)   //camera 2 matrix 3x4 double
{
    double wi = 1, wi1 = 1;
	double EPSILON = 0.0001;

	cv::Mat_<double> X(4,1);
	cv::Mat_<double> X_ = linearLSTriangulation(u,P,u1,P1);
	X(0) = X_(0); X(1) = X_(1); X(2) = X_(2); X_(3) = 1.0;
	for (int i=0; i<10; i++)  //Hartley suggests 10 iterations at most
	{
        //recalculate weights
    	double p2x = cv::Mat(cv::Mat(P).row(2)*cv::Mat(X)).at<double>(0);
    	double p2x1 = cv::Mat(cv::Mat(P1).row(2)*cv::Mat(X)).at<double>(0);

        //breaking point
        if(fabs(wi - p2x) <= EPSILON && fabs(wi1 - p2x1) <= EPSILON) break;

        wi = p2x;
        wi1 = p2x1;

        //reweight equations and solve
        cv::Mat A = (cv::Mat_<double>(4,3) <<
        		(u.x*P(2,0)-P(0,0))/wi,       (u.x*P(2,1)-P(0,1))/wi,         (u.x*P(2,2)-P(0,2))/wi,
				(u.y*P(2,0)-P(1,0))/wi,       (u.y*P(2,1)-P(1,1))/wi,         (u.y*P(2,2)-P(1,2))/wi,
				(u1.x*P1(2,0)-P1(0,0))/wi1,   (u1.x*P1(2,1)-P1(0,1))/wi1,     (u1.x*P1(2,2)-P1(0,2))/wi1,
				(u1.y*P1(2,0)-P1(1,0))/wi1,   (u1.y*P1(2,1)-P1(1,1))/wi1,     (u1.y*P1(2,2)-P1(1,2))/wi1);
        cv::Mat B = (cv::Mat_<double>(4,1) <<
        		-(u.x*P(2,3)    -P(0,3))/wi,
				-(u.y*P(2,3)  -P(1,3))/wi,
				-(u1.x*P1(2,3)    -P1(0,3))/wi1,
				-(u1.y*P1(2,3)    -P1(1,3))/wi1);

        solve(A,B,X_,cv::DECOMP_SVD);
        X(0) = X_(0); X(1) = X_(1); X(2) = X_(2); X_(3) = 1.0;
    }
    return X;
}

/**
 source = SfM toy library: https://github.com/royshil/SfM-Toy-Library
 */
//Triagulate points
double EpipolarGeometry::triangulatePoints(
		const std::vector<cv::Point2f>& pt_set1,
		const std::vector<cv::Point2f>& pt_set2,
		const cv::Mat& P, // 3x4 double
		const cv::Mat& P1, // 3x4 double
		pcl::PointCloud<pcl::PointXYZ>::Ptr & pointcloud,
		std::vector<double> & reproj_errors)
{
	pointcloud.reset(new pcl::PointCloud<pcl::PointXYZ>);

	unsigned int pts_size = pt_set1.size();

	pointcloud->resize(pts_size);
	reproj_errors.resize(pts_size);

	for(unsigned int i=0; i<pts_size; i++)
	{
		cv::Point3d u(pt_set1[i].x,pt_set1[i].y,1.0);
		cv::Point3d u1(pt_set2[i].x,pt_set2[i].y,1.0);

		cv::Mat_<double> X = iterativeLinearLSTriangulation(u,P,u1,P1);

		cv::Mat_<double> xPt_img = P1 * X;				//reproject
		cv::Point2f xPt_img_(xPt_img(0)/xPt_img(2),xPt_img(1)/xPt_img(2));

		double reprj_err = norm(xPt_img_-pt_set1[i]);
		reproj_errors[i] = reprj_err;
		pointcloud->at(i) = pcl::PointXYZ(X(0),X(1),X(2));
	}

	return cv::mean(reproj_errors)[0]; // mean reproj error
}

} // namespace rtabmap
