#pragma once

namespace DT 
{
	constexpr double eps = 1e-4;
	using DirectX::SimpleMath::Vector2;

	struct Edge 
	{
		Vector2 p0, p1;
		
		Edge(const Vector2& _p0, const Vector2& _p1) : p0{_p0}, p1{_p1} {}
		
		bool operator==(const Edge& other) const
		{
			return ((other.p0 == p0 && other.p1 == p1) || (other.p0 == p1 && other.p1 == p0));
		}
	};

	struct Circle 
	{
		Vector2 center;
		float radius;
		
		Circle() = default;
	};

	struct Triangle 
	{
		Vector2 p0, p1, p2;
		Edge e0, e1, e2;
		Circle circle;
		
		Triangle(const Vector2& _p0, const Vector2& _p1, const Vector2& _p2)
			: p0{_p0}, p1{_p1}, p2{_p2}, e0{_p0, _p1}, e1{_p1, _p2}, e2{_p0, _p2}, circle{}
		{
			const auto ax = p1.x - p0.x;
			const auto ay = p1.y - p0.y;
			const auto bx = p2.x - p0.x;
			const auto by = p2.y - p0.y;

			const auto m = p1.x * p1.x - p0.x * p0.x + p1.y * p1.y - p0.y * p0.y;
			const auto u = p2.x * p2.x - p0.x * p0.x + p2.y * p2.y - p0.y * p0.y;
			const auto s = 1. / (2. * (ax * by - ay * bx));

			circle.center.x = ((p2.y - p0.y) * m + (p0.y - p1.y) * u) * s;
			circle.center.y = ((p0.x - p2.x) * m + (p1.x - p0.x) * u) * s;

			const auto dx = p0.x - circle.center.x;
			const auto dy = p0.y - circle.center.y;
			circle.radius = dx * dx + dy * dy;
		}
	};

	struct Delaunay 
	{
		std::vector<Triangle> triangles;
		std::vector<Edge> edges;
	};

	Delaunay triangulate(const std::vector<Vector2>& points)
	{
		if (points.size() < 3) 
			return Delaunay{};

		auto xmin = points[0].x;
		auto xmax = xmin;
		auto ymin = points[0].y;
		auto ymax = ymin;
		
		for (auto const& pt : points) 
		{
			xmin = std::min(xmin, pt.x);
			xmax = std::max(xmax, pt.x);
			ymin = std::min(ymin, pt.y);
			ymax = std::max(ymax, pt.y);
		}

		const auto dx = xmax - xmin;
		const auto dy = ymax - ymin;
		const auto dmax = std::max(dx, dy);
		const auto midx = (xmin + xmax) / 2.0f;
		const auto midy = (ymin + ymax) / 2.0f;

		/* Init Delaunay triangulation. */
		auto d = Delaunay{};

		const auto p0 = Vector2{midx - 20 * dmax, midy - dmax};
		const auto p1 = Vector2{midx, midy + 20 * dmax};
		const auto p2 = Vector2{midx + 20 * dmax, midy - dmax};
		d.triangles.emplace_back(Triangle{p0, p1, p2});

		for (auto const& pt : points) 
		{
			std::vector<Edge> edges;
			std::vector<Triangle> tmps;
			for (auto const& tri : d.triangles) 
			{
				/* Check if the point is inside the triangle circumcircle. */
				const auto dist = Vector2::DistanceSquared(tri.circle.center, pt);
				if ((dist - tri.circle.radius) <= eps) 
				{
					edges.push_back(tri.e0);
					edges.push_back(tri.e1);
					edges.push_back(tri.e2);
				}
				else 
				{
					tmps.push_back(tri);
				}
			}

			/* Delete duplicate edges. */
			std::vector<bool> remove(edges.size(), false);
			for (auto it1 = edges.begin(); it1 != edges.end(); ++it1)
			{
				for (auto it2 = edges.begin(); it2 != edges.end(); ++it2) 
				{
					if (it1 == it2) 
						continue;
					
					if (*it1 == *it2) 
					{
						remove[std::distance(edges.begin(), it1)] = true;
						remove[std::distance(edges.begin(), it2)] = true;
					}
				}
			}

			edges.erase(std::remove_if(edges.begin(), edges.end(), [&](auto const& e) { return remove[&e - &edges[0]]; }), edges.end());

			/* Update triangulation. */
			for (auto const& e : edges) 
				tmps.push_back({ e.p0, e.p1, {pt.x, pt.y} });
			
			d.triangles = tmps;
		}

		/* Remove original super triangle. */
		d.triangles.erase(std::remove_if(d.triangles.begin(), d.triangles.end(), 
			[&](auto const& tri) 
			{
				return ((tri.p0 == p0 || tri.p1 == p0 || tri.p2 == p0) ||
						(tri.p0 == p1 || tri.p1 == p1 || tri.p2 == p1) ||
						(tri.p0 == p2 || tri.p1 == p2 || tri.p2 == p2));
			}), d.triangles.end());

		/* Add edges. */
		for (auto const& tri : d.triangles) 
		{
			d.edges.push_back(tri.e0);
			d.edges.push_back(tri.e1);
			d.edges.push_back(tri.e2);
		}
		
		return d;
	}
}