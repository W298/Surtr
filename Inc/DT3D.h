#pragma once

#include "VMACH.h"

namespace DT3D
{
	using DirectX::SimpleMath::Vector3;
	using DirectX::SimpleMath::Matrix;

	void tetrahedron_circumcenter(
		// In:
		const double a[3],
		const double b[3],
		const double c[3],
		const double d[3],
		// Out:
		double circumcenter[3],
		double* xi,
		double* eta,
		double* zeta)
	{
		double denominator;


		// Use coordinates relative to point 'a' of the tetrahedron.

		// ba = b - a
		double ba_x = b[0] - a[0];
		double ba_y = b[1] - a[1];
		double ba_z = b[2] - a[2];

		// ca = c - a
		double ca_x = c[0] - a[0];
		double ca_y = c[1] - a[1];
		double ca_z = c[2] - a[2];

		// da = d - a
		double da_x = d[0] - a[0];
		double da_y = d[1] - a[1];
		double da_z = d[2] - a[2];

		// Squares of lengths of the edges incident to 'a'.
		double len_ba = ba_x * ba_x + ba_y * ba_y + ba_z * ba_z;
		double len_ca = ca_x * ca_x + ca_y * ca_y + ca_z * ca_z;
		double len_da = da_x * da_x + da_y * da_y + da_z * da_z;

		// Cross products of these edges.

		// c cross d
		double cross_cd_x = ca_y * da_z - da_y * ca_z;
		double cross_cd_y = ca_z * da_x - da_z * ca_x;
		double cross_cd_z = ca_x * da_y - da_x * ca_y;

		// d cross b
		double cross_db_x = da_y * ba_z - ba_y * da_z;
		double cross_db_y = da_z * ba_x - ba_z * da_x;
		double cross_db_z = da_x * ba_y - ba_x * da_y;

		// b cross c
		double cross_bc_x = ba_y * ca_z - ca_y * ba_z;
		double cross_bc_y = ba_z * ca_x - ca_z * ba_x;
		double cross_bc_z = ba_x * ca_y - ca_x * ba_y;

		// Calculate the denominator of the formula.
		denominator = 0.5 / (ba_x * cross_cd_x + ba_y * cross_cd_y + ba_z * cross_cd_z);

		// Calculate offset (from 'a') of circumcenter.
		double circ_x = (len_ba * cross_cd_x + len_ca * cross_db_x + len_da * cross_bc_x) * denominator;
		double circ_y = (len_ba * cross_cd_y + len_ca * cross_db_y + len_da * cross_bc_y) * denominator;
		double circ_z = (len_ba * cross_cd_z + len_ca * cross_db_z + len_da * cross_bc_z) * denominator;

		circumcenter[0] = circ_x;
		circumcenter[1] = circ_y;
		circumcenter[2] = circ_z;

		if (xi != (double*) nullptr) {
			// To interpolate a linear function at the circumcenter, define a
			// coordinate system with a xi-axis directed from 'a' to 'b',
			// an eta-axis directed from 'a' to 'c', and a zeta-axis directed
			// from 'a' to 'd'.  The values for xi, eta, and zeta are computed
			// by Cramer's Rule for solving systems of linear equations.
			denominator *= 2.0;
			*xi = (circ_x * cross_cd_x + circ_y * cross_cd_y + circ_z * cross_cd_z) * denominator;
			*eta = (circ_x * cross_db_x + circ_y * cross_db_y + circ_z * cross_db_z) * denominator;
			*zeta = (circ_x * cross_bc_x + circ_y * cross_bc_y + circ_z * cross_bc_z) * denominator;
		}
	}

	struct Sphere
	{
		Vector3 center;
		float radius;
	};

	struct Edge
	{
		Vector3 p0, p1;

		Edge(const Vector3& _p0, const Vector3& _p1) : p0(_p0), p1(_p1) {}

		bool operator==(const Edge& other) const
		{
			return ((other.p0 == p0 && other.p1 == p1) || (other.p0 == p1 && other.p1 == p0));
		}
	};

	struct Triangle
	{
		Vector3 p0, p1, p2;

		Triangle() = default;
		Triangle(const Vector3& _p0, const Vector3& _p1, const Vector3& _p2) : p0(_p0), p1(_p1), p2(_p2) {}

		bool operator==(const Triangle& o) const
		{
			return (
				(o.p0 == p0 && o.p1 == p1 && o.p2 == p2) || 
				(o.p0 == p0 && o.p1 == p2 && o.p2 == p1) ||
				(o.p0 == p1 && o.p1 == p0 && o.p2 == p2) ||
				(o.p0 == p1 && o.p1 == p2 && o.p2 == p0) ||
				(o.p0 == p2 && o.p1 == p0 && o.p2 == p1) ||
				(o.p0 == p2 && o.p1 == p1 && o.p2 == p0));
		}
	};

	struct Tetrahedron
	{
		Vector3 p0, p1, p2, p3;
		Triangle t0, t1, t2, t3;
		Sphere sphere;

		Tetrahedron(Vector3 _p0, Vector3 _p1, Vector3 _p2, Vector3 _p3) : p0(_p0), p1(_p1), p2(_p2), p3(_p3) 
		{
			t0 = Triangle(p0, p1, p2);
			t1 = Triangle(p0, p1, p3);
			t2 = Triangle(p1, p2, p3);
			t3 = Triangle(p2, p0, p3);

			const double a[] = { p0.x, p0.y, p0.z };
			const double b[] = { p1.x, p1.y, p1.z };
			const double c[] = { p2.x, p2.y, p2.z };
			const double d[] = { p3.x, p3.y, p3.z };

			double circumcenter[3];

			tetrahedron_circumcenter(a, b, c, d, circumcenter, nullptr, nullptr, nullptr);

			sphere.center = Vector3(a[0] + circumcenter[0], a[1] + circumcenter[1], a[2] + circumcenter[2]);
			sphere.radius = Vector3::Distance(p0, sphere.center);
		}
	};

	struct Delaunay
	{
		std::vector<Tetrahedron> TetVec;
		std::vector<Triangle> FaceVec;
	};

	Delaunay Triangulate(const std::vector<Vector3>& points)
	{
		if (points.size() < 3)
			return Delaunay();

		Delaunay dt;

		auto xmin = points[0].x;
		auto xmax = xmin;
		auto ymin = points[0].y;
		auto ymax = ymin;
		auto zmin = points[0].z;
		auto zmax = zmin;

		for (auto const& pt : points)
		{
			xmin = std::min(xmin, pt.x);
			xmax = std::max(xmax, pt.x);
			ymin = std::min(ymin, pt.y);
			ymax = std::max(ymax, pt.y);
			zmin = std::min(zmin, pt.z);
			zmax = std::max(zmax, pt.z);
		}

		const auto dx = xmax - xmin;
		const auto dy = ymax - ymin;
		const auto dz = zmax - zmin;
		const auto dmax = std::max(std::max(dx, dy), dz);
		const auto midx = (xmin + xmax) / 2.0f;
		const auto midy = (ymin + ymax) / 2.0f;
		const auto midz = (zmin + zmax) / 2.0f;
		
		const auto p0 = Vector3{ midx - 20 * dmax,	midy - dmax,		midz - dmax };
		const auto p1 = Vector3{ midx,				midy - dmax,		midz + 20 * dmax };
		const auto p2 = Vector3{ midx + 20 * dmax,  midy - dmax,		midz - dmax };
		const auto p3 = Vector3{ midx,				midy + 20 * dmax,	midz };

		dt.TetVec.emplace_back(p0, p1, p2, p3);

		for (int i = 0; i < points.size(); i++)
		{
			const auto pt = points[i];

			std::vector<Triangle> faces;
			std::vector<Tetrahedron> tmps;
			for (auto const& tet : dt.TetVec)
			{
				// #CORRECTION
				// Check if the point is inside the tetrahedron circumsphere.
				const auto dist = Vector3::Distance(tet.sphere.center, pt);
				if ((dist - tet.sphere.radius) <= EPSILON)
				{
					faces.push_back(tet.t0);
					faces.push_back(tet.t1);
					faces.push_back(tet.t2);
					faces.push_back(tet.t3);
				}
				else
				{
					tmps.push_back(tet);
				}
			}

			// Delete duplicate faces.
			std::vector<bool> remove(faces.size(), false);
			for (auto it1 = faces.begin(); it1 != faces.end(); ++it1)
			{
				for (auto it2 = faces.begin(); it2 != faces.end(); ++it2)
				{
					if (it1 == it2)
						continue;

					if (*it1 == *it2)
					{
						remove[std::distance(faces.begin(), it1)] = true;
						remove[std::distance(faces.begin(), it2)] = true;
					}
				}
			}

			faces.erase(std::remove_if(faces.begin(), faces.end(), [&](auto const& f) { return remove[&f - &faces[0]]; }), faces.end());

			// Update triangulation.
			for (auto const& f : faces)
				tmps.emplace_back(f.p0, f.p1, f.p2, pt);

			dt.TetVec = tmps;
		}

		// Remove original super triangle.
		dt.TetVec.erase(std::remove_if(dt.TetVec.begin(), dt.TetVec.end(),
			[&](auto const& tet)
			{
				return ((tet.p0 == p0 || tet.p1 == p0 || tet.p2 == p0 || tet.p3 == p0) ||
						(tet.p0 == p1 || tet.p1 == p1 || tet.p2 == p1 || tet.p3 == p1) ||
						(tet.p0 == p2 || tet.p1 == p2 || tet.p2 == p2 || tet.p3 == p2) ||
						(tet.p0 == p3 || tet.p1 == p3 || tet.p2 == p3 || tet.p3 == p3));
			}), dt.TetVec.end());

		// Add faces.
		for (auto const& tet : dt.TetVec)
		{
			dt.FaceVec.push_back(tet.t0);
			dt.FaceVec.push_back(tet.t1);
			dt.FaceVec.push_back(tet.t2);
		}

		return dt;
	}

	std::vector<Edge> Voronoi(const Delaunay& dt)
	{
		std::vector<Edge> edgeVec;

		for (int i = 0; i < dt.TetVec.size(); i++)
		{
			int adjFaceCnt = 0;
			for (int j = 0; j < dt.TetVec.size(); j++)
			{
				if (i == j)
					continue;

				if (
					(dt.TetVec[i].t0 == dt.TetVec[j].t0) ||
					(dt.TetVec[i].t0 == dt.TetVec[j].t1) ||
					(dt.TetVec[i].t0 == dt.TetVec[j].t2) ||
					(dt.TetVec[i].t0 == dt.TetVec[j].t3) ||
					(dt.TetVec[i].t1 == dt.TetVec[j].t0) ||
					(dt.TetVec[i].t1 == dt.TetVec[j].t1) ||
					(dt.TetVec[i].t1 == dt.TetVec[j].t2) ||
					(dt.TetVec[i].t1 == dt.TetVec[j].t3) ||
					(dt.TetVec[i].t2 == dt.TetVec[j].t0) ||
					(dt.TetVec[i].t2 == dt.TetVec[j].t1) ||
					(dt.TetVec[i].t2 == dt.TetVec[j].t2) ||
					(dt.TetVec[i].t2 == dt.TetVec[j].t3) ||
					(dt.TetVec[i].t3 == dt.TetVec[j].t0) ||
					(dt.TetVec[i].t3 == dt.TetVec[j].t1) ||
					(dt.TetVec[i].t3 == dt.TetVec[j].t2) ||
					(dt.TetVec[i].t3 == dt.TetVec[j].t3))
				{
					edgeVec.emplace_back(
						dt.TetVec[i].sphere.center,
						dt.TetVec[j].sphere.center);
					
					adjFaceCnt++;
				}

				if (adjFaceCnt >= 4)
					break;
			}
		}

		std::vector<DT3D::Edge> uniqueEdgeVec;
		Unique<DT3D::Edge>(edgeVec, uniqueEdgeVec);

		return uniqueEdgeVec;
	}
}