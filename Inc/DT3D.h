#pragma once

namespace DT3D
{
	constexpr double eps = 1e-4;
	using DirectX::SimpleMath::Vector3;
	using DirectX::SimpleMath::Matrix;

	struct Edge
	{
		Vector3 p0, p1;

		Edge() = default;
		Edge(const Vector3& _p0, const Vector3& _p1) : p0{ _p0 }, p1{ _p1 } {}

		bool operator==(const Edge& other) const
		{
			return ((other.p0 == p0 && other.p1 == p1) || (other.p0 == p1 && other.p1 == p0));
		}
	};

	struct Sphere
	{
		Vector3 center;
		float radius;

		Sphere() = default;
	};

	struct Triangle
	{
		Vector3 p0, p1, p2;
		Edge e0, e1, e2;

		Triangle() = default;
		Triangle(const Vector3& _p0, const Vector3& _p1, const Vector3& _p2)
			: p0{ _p0 }, p1{ _p1 }, p2{ _p2 }, e0{ _p0, _p1 }, e1{ _p1, _p2 }, e2{ _p0, _p2 } {}

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
		Edge e0, e1, e2, e3, e4, e5;
		Triangle t0, t1, t2, t3;
		Sphere sphere;

		Tetrahedron(Vector3 _p0, Vector3 _p1, Vector3 _p2, Vector3 _p3) : p0(_p0), p1(_p1), p2(_p2), p3(_p3) 
		{
			t0 = Triangle(p0, p1, p2);
			t1 = Triangle(p0, p1, p3);
			t2 = Triangle(p1, p2, p3);
			t3 = Triangle(p2, p0, p3);

			double x1 = p0.x;
			double x2 = p1.x;
			double x3 = p2.x;
			double x4 = p3.x;

			double y1 = p0.y;
			double y2 = p1.y;
			double y3 = p2.y;
			double y4 = p3.y;

			double z1 = p0.z;
			double z2 = p1.z;
			double z3 = p2.z;
			double z4 = p3.z;

			double xyz1 = x1 * x1 + y1 * y1 + z1 * z1;
			double xyz2 = x2 * x2 + y2 * y2 + z2 * z2;
			double xyz3 = x3 * x3 + y3 * y3 + z3 * z3;
			double xyz4 = x4 * x4 + y4 * y4 + z4 * z4;

			Matrix a
			(
				x1, y1, z1, 1,
				x2, y2, z2, 1,
				x3, y3, z3, 1,
				x4, y4, z4, 1
			);

			Matrix dx
			(
				xyz1, y1, z1, 1,
				xyz2, y2, z2, 1,
				xyz3, y3, z3, 1,
				xyz4, y4, z4, 1
			);

			Matrix dy
			(
				xyz1, x1, z1, 1,
				xyz2, x2, z2, 1,
				xyz3, x3, z3, 1,
				xyz4, x4, z4, 1
			);

			Matrix dz
			(
				xyz1, x1, y1, 1,
				xyz2, x2, y2, 1,
				xyz3, x3, y3, 1,
				xyz4, x4, y4, 1
			);

			Matrix c
			(
				xyz1, x1, y1, z1,
				xyz2, x2, y2, z2,
				xyz3, x3, y3, z3,
				xyz4, x4, y4, z4
			);

			sphere.center = Vector3(
				dx.Determinant() / (2 * a.Determinant()), 
				dy.Determinant() / (2 * a.Determinant()), 
				dz.Determinant() / (2 * a.Determinant()));

			sphere.radius = 
				std::sqrt(
					std::pow(dx.Determinant(), 2) + 
					std::pow(dy.Determinant(), 2) + 
					std::pow(dz.Determinant(), 2) - 4 * a.Determinant() * c.Determinant()) / (2 * std::abs(a.Determinant()));
		}
	};

	struct Delaunay
	{
		std::vector<Tetrahedron> tetrahedrons;
		std::vector<Triangle> faces;
	};

	Delaunay triangulate(const std::vector<Vector3>& points)
	{
		if (points.size() < 3)
			return Delaunay{};

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

		/* Init Delaunay triangulation. */
		auto d = Delaunay{};

		const auto p0 = Vector3{ midx - 20 * dmax,	midy - dmax,		midz - dmax };
		const auto p1 = Vector3{ midx,				midy - dmax,		midz + 20 * dmax };
		const auto p2 = Vector3{ midx + 20 * dmax,  midy - dmax,		midz - dmax };
		const auto p3 = Vector3{ midx,				midy + 20 * dmax,	midz };

		d.tetrahedrons.emplace_back(p0, p1, p2, p3);

		for (int i = 0; i < points.size(); i++)
		{
			const auto pt = points[i];

			std::vector<Triangle> faces;
			std::vector<Tetrahedron> tmps;
			for (auto const& tet : d.tetrahedrons)
			{
				/* Check if the point is inside the triangle circumcircle. */
				const auto dist = Vector3::Distance(tet.sphere.center, pt);
				if ((dist - tet.sphere.radius) <= eps)
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

			/* Delete duplicate faces. */
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

			/* Update triangulation. */
			for (auto const& f : faces)
				tmps.emplace_back(f.p0, f.p1, f.p2, pt);

			d.tetrahedrons = tmps;
		}

		/* Remove original super triangle. */
		d.tetrahedrons.erase(std::remove_if(d.tetrahedrons.begin(), d.tetrahedrons.end(),
			[&](auto const& tet)
			{
				return ((tet.p0 == p0 || tet.p1 == p0 || tet.p2 == p0 || tet.p3 == p0) ||
						(tet.p0 == p1 || tet.p1 == p1 || tet.p2 == p1 || tet.p3 == p1) ||
						(tet.p0 == p2 || tet.p1 == p2 || tet.p2 == p2 || tet.p3 == p2) ||
						(tet.p0 == p3 || tet.p1 == p3 || tet.p2 == p3 || tet.p3 == p3));
			}), d.tetrahedrons.end());

		/* Add faces. */
		for (auto const& tet : d.tetrahedrons)
		{
			d.faces.push_back(tet.t0);
			d.faces.push_back(tet.t1);
			d.faces.push_back(tet.t2);
		}

		return d;
	}
}