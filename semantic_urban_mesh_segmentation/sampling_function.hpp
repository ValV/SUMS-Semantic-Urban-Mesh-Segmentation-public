/*
*   Name        : sampling_function.hpp
*   Author      : Weixiao GAO
*   Date        : 15/09/2021
*   Version     : 1.0
*   Description : for point cloud sampling
*   Availability:
*   Copyright   : Copyright (C) 2021 by Weixiao GAO (gaoweixiaocuhk@gmail.com)
*                 All rights reserved.
*
*				  This file is part of semantic_urban_mesh_segmentation: software
*				  for semantic segmentation of textured urban meshes.
*
*				  semantic_urban_mesh_segmentation is free software; you can
*				  redistribute it and/or modify it under the terms of the GNU
*				  General Public License Version 3 as published by the Free
*				  Software Foundation.
*
*				  semantic_urban_mesh_segmentation is distributed in the hope that
*				  it will be useful, but WITHOUT ANY WARRANTY; without even the
*				  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
*				  PURPOSE. See the GNU General Public License for more details.
*
*				  You should have received a copy of the GNU General Public License
*				  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#ifndef semantic_mesh_segmentation__SAMPLING_FUNCTION_HPP
#define semantic_mesh_segmentation__SAMPLING_FUNCTION_HPP
#include <algorithm>
#include <random>
#include <easy3d/point_cloud.h>
#include <easy3d/kdtree.h>
#include <easy3d/point_cloud_simplification.h>
#include "super_segment.hpp"
#include "math_base.hpp"
#include "random_generator.hpp"
#include "spatial_hashing.hpp"

namespace semantic_mesh_segmentation
{
	typedef typename SpatialHashTable<easy3d::vec3> MontecarloSHT;
	typedef typename SpatialHashTable<easy3d::vec3>::CellIterator MontecarloSHTIterator;
	typedef typename SpatialHashTable<easy3d::vec3> SampleSHT;
	typedef typename SpatialHashTable<easy3d::vec3>::CellIterator SampleSHTIterator;

	// \brief Estimate the radius r that you should give to get a certain number of samples in a Poissson Disk Distribution of radius r.
	inline float ComputePoissonDiskRadius(const float &area_temp, const int sampling_points_number)
	{
		return sqrt(area_temp / (0.7 * M_PI * sampling_points_number));// 0.7 is a density factor
	}

	inline random_generator::MarsenneTwisterRNG &SamplingRandomGenerator()
	{
		static random_generator::MarsenneTwisterRNG rnd;//make it not change each time when use it.
		return rnd;
	}

	// Returns an integer random number in the [0,i-1] interval using the improve Marsenne-Twister method.
	// this functor is needed for passing it to the std functions.
	inline unsigned int RandomInt(unsigned int i)
	{
		return (SamplingRandomGenerator().generate(i));
	}

	class MarsenneTwisterURBG
	{
	public:
		typedef unsigned int result_type;
		MarsenneTwisterURBG(result_type max) { _max = max; }
		static constexpr result_type min() { return 0; }
		static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }
		result_type operator()() { return SamplingRandomGenerator().generate(_max); }
	private:
		result_type _max;
	};

	inline double RandomDouble01()
	{
		return SamplingRandomGenerator().generate01();
	}

	inline unsigned int GridGetInBox
	(
		SampleSHT &_Si,
		const easy3d::Box3 &_bbox,
		std::vector<easy3d::vec3> & _objectPtrs
	)
	{
		typename SampleSHT::CellIterator first, last, l;
		_objectPtrs.clear();
		easy3d::GenericBox3<int> ibbox;
		easy3d::GenericBox3<int> Si_ibox(easy3d::Vec<3, int>(0, 0, 0), _Si.siz - easy3d::Vec<3, int>(1, 1, 1));
		_Si.BoxToIBox(_bbox, ibbox);
		ibbox.Intersect(Si_ibox);
		//_marker.UnMarkAll();

		if (ibbox.IsNull())
			return 0;
		else
		{
			int ix, iy, iz;
			for (ix = ibbox.x_min(); ix <= ibbox.x_max(); ix++)
			{
				for (iy = ibbox.y_min(); iy <= ibbox.y_max(); iy++)
				{
					for (iz = ibbox.z_min(); iz <= ibbox.z_max(); iz++)
					{
						_Si.Grid(ix, iy, iz, first, last);
						for (l = first; l != last; ++l)
						{
							easy3d::Box3 box_elem;
							box_elem.add_point(**l);
							if ((box_elem.Collide(_bbox))) //(!_marker.IsMarked(elem)) && 
							{
								_objectPtrs.push_back(**l);
								//_marker.Mark(elem);
							}
						}
					}
				}
			}
			return (static_cast<unsigned int>(_objectPtrs.size()));
		}
	}

	// check the radius constrain
	inline bool checkPoissonDisk(SampleSHT &sht, const easy3d::vec3 &p, float radius)
	{
		//get the samples closest to the given one
		std::vector<easy3d::vec3> closests;

		easy3d::Box3 bb(p - easy3d::vec3(radius, radius, radius), p + easy3d::vec3(radius, radius, radius));
		auto nsamples = GridGetInBox(sht, bb, closests);

		float r2 = radius * radius;
		for (int i = 0; i < closests.size(); ++i)
			if (easy3d::distance(p, closests[i]) < r2)
				return false;

		return true;
	}

	// initialize spatial hash table for searching
	// radius is the radius of empty disk centered over the samples (e.g. twice of the empty space disk)
	// This radius implies that when we pick a sample in a cell all that cell probably will not be touched again.
	// Howvever we must ensure that we do not put too many vertices inside each hash cell
	static void InitSpatialHashTable
	(
		easy3d::PointCloud *montecarloMesh,
		SFMesh* smesh,
		MontecarloSHT &montecarloSHT,
		float diskRadius
	)
	{
		float cellsize = 2.0f* diskRadius / sqrt(3.0);

		float occupancyRatio = 0;
		do
		{
			// inflating
			easy3d::Box3 extend_box = smesh->mesh_bbox;
			extend_box.Offset(cellsize);

			int sizeX = 1.0f > (extend_box.x_range() / cellsize) ? 1.0f : (extend_box.x_range() / cellsize);
			int sizeY = 1.0f > (extend_box.y_range() / cellsize) ? 1.0f : (extend_box.y_range() / cellsize);
			int sizeZ = 1.0f > (extend_box.z_range() / cellsize) ? 1.0f : (extend_box.z_range() / cellsize);
			easy3d::Vec<3, int> gridsize(sizeX, sizeY, sizeZ);

			//// spatial hash table of the generated samples - used to check the radius constrain
			montecarloSHT.InitEmpty(extend_box, gridsize);

			// create active cell list
			auto points_coord = montecarloMesh->get_vertex_property<easy3d::vec3>("v:point");
			for (auto vi : montecarloMesh->vertices())
			{
				montecarloSHT.Add(&points_coord[vi]);
			}
			montecarloSHT.UpdateAllocatedCells();

			int gridCellNum = (int)montecarloSHT.AllocatedCells.size();
			cellsize /= 2.0f;
			occupancyRatio = float(montecarloMesh->vertices_size()) / float(montecarloSHT.AllocatedCells.size());
		} while (occupancyRatio > 100);
	}

	// Given a cell of the grid it search the point that remove the minimum number of other samples
	// it linearly scan all the points of a cell.
	static easy3d::vec3* getBestPrecomputedMontecarloSample
	(
		easy3d::Vec<3, int> &cell,
		MontecarloSHT & samplepool,
		const float &diskRadius,
		const int bestSamplePoolSize
	)
	{
		MontecarloSHTIterator cellBegin, cellEnd;
		samplepool.Grid(cell, cellBegin, cellEnd);
		easy3d::vec3* bestSample;
		int minRemoveCnt = std::numeric_limits<int>::max();
		std::vector<typename MontecarloSHT::HashIterator> inSphVec;
		int i = 0;
		for (MontecarloSHTIterator ci = cellBegin; ci != cellEnd && i < bestSamplePoolSize; ++ci, i++)
		{
			easy3d::vec3* sp = *ci;//;ci.get_result();
			int curRemoveCnt = samplepool.CountInSphere(*sp, diskRadius, inSphVec);
			if (curRemoveCnt < minRemoveCnt)
			{
				bestSample = sp;
				minRemoveCnt = curRemoveCnt;
			}
		}
		return bestSample;
	}

	// Trivial approach that puts all the samples in a UG and removes all the ones that surely do not fit the
	inline void PoissonDiskPruning
	(
		easy3d::PointCloud* possion_pointcloud,
		easy3d::PointCloud *sampling_pointcloud,
		SFMesh* smesh,
		int& sampleNum,
		float diskRadius,
		const int bestSamplePoolSize
	)
	{
		//*****************
		//easy3d::PointCloud* possion_pointcloud = new easy3d::PointCloud;
		MontecarloSHT montecarloSHT;
		InitSpatialHashTable(sampling_pointcloud, smesh, montecarloSHT, diskRadius);

		// if we are doing variable density sampling we have to prepare the handle that keeps the the random samples expected radii.
		// At this point we just assume that there is the quality values as sampled from the base mesh
		// shuffle active cells
		//unsigned int(*p_myrandom)(unsigned int) = RandomInt;
		//std::shuffle(montecarloSHT.AllocatedCells.begin(), montecarloSHT.AllocatedCells.end(), std::mt19937(std::random_device()()));
		MarsenneTwisterURBG g(montecarloSHT.AllocatedCells.size());
		std::shuffle(montecarloSHT.AllocatedCells.begin(), montecarloSHT.AllocatedCells.end(), g);

		int montecarloSampleNum = sampling_pointcloud->n_vertices();
		sampleNum = 0;
		int removedCnt = 0;

		auto points_coord = smesh->get_vertex_property<easy3d::vec3>("v:point");
		for (auto vi : smesh->vertices())
		{
			possion_pointcloud->add_vertex(points_coord[vi]);
			sampleNum++;
			removedCnt += montecarloSHT.RemoveInSphere(points_coord[vi], diskRadius);
		}
		montecarloSHT.UpdateAllocatedCells();

		while (!montecarloSHT.AllocatedCells.empty())
		{
			removedCnt = 0;
			for (size_t i = 0; i < montecarloSHT.AllocatedCells.size(); i++)
			{
				if (montecarloSHT.EmptyCell(montecarloSHT.AllocatedCells[i])) 
					continue;
				float currentRadius = diskRadius;
				easy3d::vec3* sp = getBestPrecomputedMontecarloSample(montecarloSHT.AllocatedCells[i], montecarloSHT, diskRadius, bestSamplePoolSize);

				possion_pointcloud->add_vertex(*sp);
				sampleNum++;
				removedCnt += montecarloSHT.RemoveInSphere(*sp, currentRadius);
			}
			montecarloSHT.UpdateAllocatedCells();
		}
	}


	inline void PoissonDiskPruningByNumber
	(
		easy3d::PointCloud* possion_cloud,
		easy3d::PointCloud* montecarlo_cloud,
		SFMesh* smesh,
		const int sampling_points_number,
		float& diskRadius,
		const float tolerance,
		const int bestSamplePoolSize,
		const int maxIter
	)
	{
		size_t sampleNumMin = int(float(sampling_points_number) * (1.0f - tolerance));
		size_t sampleNumMax = int(float(sampling_points_number) * (1.0f + tolerance));
		float RangeMinRad = smesh->mesh_bbox.diagonal() / 50.0;
		float RangeMaxRad = smesh->mesh_bbox.diagonal() / 50.0;
		size_t RangeMinRadNum;
		size_t RangeMaxRadNum;
		int pp_sampleNum = 0;
		// Note     RangeMinRad          <       RangeMaxRad
	   //  but      RangeMinRadNum > sampleNum > RangeMaxRadNum
		do {
			possion_cloud->clear();
			RangeMinRad /= 2.0f;
			PoissonDiskPruning(possion_cloud, montecarlo_cloud, smesh, pp_sampleNum, RangeMinRad, bestSamplePoolSize);
			RangeMinRadNum = pp_sampleNum;
		} while (RangeMinRadNum < sampling_points_number); // if the number of sample is still smaller you have to make radius larger.

		do {
			possion_cloud->clear();
			RangeMaxRad *= 2.0f;
			PoissonDiskPruning(possion_cloud, montecarlo_cloud, smesh, pp_sampleNum, RangeMaxRad, bestSamplePoolSize);
			RangeMaxRadNum = pp_sampleNum;
		} while (RangeMaxRadNum > sampling_points_number);


		float curRadius = RangeMaxRad;
		int iterCnt = 0;
		while (iterCnt < maxIter &&
			(pp_sampleNum < sampleNumMin || pp_sampleNum  > sampleNumMax))
		{
			iterCnt++;
			possion_cloud->clear();
			curRadius = (RangeMaxRad + RangeMinRad) / 2.0f;
			PoissonDiskPruning(possion_cloud, montecarlo_cloud, smesh, pp_sampleNum, curRadius, bestSamplePoolSize);
			//    qDebug("PoissonDiskPruning Iteratin (%6.3f:%5lu %6.3f:%5lu) Cur Radius %f -> %lu sample instead of %lu",RangeMinRad,RangeMinRadNum,RangeMaxRad,RangeMaxRadNum,curRadius,pp.pds.sampleNum,sampleNum);
			if (pp_sampleNum > sampling_points_number)
			{
				RangeMinRad = curRadius;
				RangeMinRadNum = pp_sampleNum;
			}
			if (pp_sampleNum < sampling_points_number)
			{
				RangeMaxRad = curRadius;
				RangeMaxRadNum = pp_sampleNum;
			}
		}
		diskRadius = curRadius;
	}

	//Montecarlo point set sampling from the mesh
	inline void montecarlo_sampling
	(
		SFMesh *smesh_out,
		easy3d::PointCloud* sampling_pointcloud,
		int used_sampling_points_number
	)
	{
		smesh_out->get_points_coord = smesh_out->get_vertex_property<vec3>("v:point");
		typedef  std::pair<float, SFMesh::Face> IntervalType;
		std::vector< IntervalType > intervals(smesh_out->faces_size() + 1);
		int i = 0;
		intervals[i] = std::make_pair(0, *smesh_out->faces_begin());
		// First loop: build a sequence of consecutive segments proportional to the triangle areas.
		for (auto fi : smesh_out->faces())
		{
			intervals[i + 1] = std::make_pair(intervals[i].first + smesh_out->get_face_area[fi], fi);
			++i;
		}

		float meshArea = intervals.back().first;

		for (i = 0; i < used_sampling_points_number; ++i)
		{
			float val = meshArea * RandomDouble01();
			// lower_bound returns the furthermost iterator i in [first, last) such that, for every iterator j in [first, i), *j < value.
			// E.g. An iterator pointing to the first element "not less than" val, or end() if every element is less than val.
			typename std::vector<IntervalType>::iterator it = lower_bound(intervals.begin(), intervals.end(), std::make_pair(val, *smesh_out->faces_begin()));
			assert(it != intervals.end());
			assert(it != intervals.begin());
			assert((*(it - 1)).first < val);
			assert((*(it)).first >= val);

			vec3 v0, v1, v2;
			int ind = 0;
			for (auto v : smesh_out->vertices((*it).second))
			{
				if (ind == 0)
					v0 = smesh_out->get_points_coord[v];
				else if (ind == 1)
					v1 = smesh_out->get_points_coord[v];
				else if (ind == 2)
					v2 = smesh_out->get_points_coord[v];
				++ind;
			}

			vec3 temp = random_generator::RandomBarycentric(SamplingRandomGenerator());
			sampling_pointcloud->add_vertex(v0 * temp[0] + v1 * temp[1] + v2 * temp[2]);
		}
	}

	void sampling_pointcloud_on_mesh
	(
		easy3d::PointCloud*, 
		SFMesh *, 
		const float,
		const float tolerance = 0.005,
		const int bestSamplePoolSize = 10,
		const int MontecarloRate = 20
	);

	void random_sampling_pointcloud_on_selected_faces
	(
		SFMesh* ,
		std::vector<SFMesh::Face>& ,
		easy3d::PointCloud* ,
		const float 
	);

	void face_center_point_cloud
	(
		SFMesh* ,
		easy3d::PointCloud* 
	);

	void assign_texpcl_properties
	(
		easy3d::PointCloud*,
		easy3d::PointCloud*
	);

	void face_random_sampling
	(
		SFMesh*,
		easy3d::SurfaceMesh::Face&,
		easy3d::PointCloud*,
		const int
	);

	void mesh_random_sampling
	(
		SFMesh*,
		easy3d::PointCloud*,
		const int
	);

	void perform_uniform_sampling(easy3d::PointCloud*, const int);

	void poission_sampling_with_fixed_number
	(
		SFMesh*,
		easy3d::PointCloud*,
		const int,
		const float tolerance = 0.005,
		const int bestSamplePoolSize = 10,
		const int MontecarloRate = 20
	);
}

#endif