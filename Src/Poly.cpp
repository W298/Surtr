#include "pch.h"
#include "Poly.h"

#include "VMACH.h"

Poly::Vertex::Vertex(const Vertex& rhs)
	: Position(rhs.Position), NeighborVertexVec(rhs.NeighborVertexVec), comp(rhs.comp), ID(rhs.ID)
{
}

Poly::Vertex::Vertex() : Position(Vector3(0, 0, 0)), comp(1), ID(-1) {}
Poly::Vertex::Vertex(const Vector3& pos) : Position(pos), comp(1), ID(-1) {}
Poly::Vertex::Vertex(const Vector3& pos, const int c) : Position(pos), comp(c), ID(-1) {}

Poly::Vertex& Poly::Vertex::operator=(const Vertex& rhs)
{
	Position = rhs.Position;
	NeighborVertexVec = rhs.NeighborVertexVec;
	comp = rhs.comp;
	ID = rhs.ID;

	return *this;
}

bool Poly::Vertex::operator==(const Vertex& rhs) const
{
	return (Position == rhs.Position && NeighborVertexVec == rhs.NeighborVertexVec && comp == rhs.comp && ID == rhs.ID);
}

// Internal Functions.
double sgn(const double x) { return (x >= 0.0 ? 1.0 : -1.0); }
double sgn0(const double x) { return (x > 0.0 ? 1.0 : x < 0.0 ? -1.0 : 0.0); }
template <typename Value> Value safeInv(const Value& x, const double fuzz = 1.0e-30) { return sgn(x) / std::max(fuzz, std::abs(x)); }
int FaceLoop(const Poly::Vertex& v, const int vprev)
{
	const auto itr = std::find(v.NeighborVertexVec.begin(), v.NeighborVertexVec.end(), vprev);
	if (itr == v.NeighborVertexVec.begin())
		return v.NeighborVertexVec.back();
	else
		return *(itr - 1);
}

void Poly::InitPolyhedron(Polyhedron& polyhedron, const std::vector<Vector3>& positionVec,
						  const std::vector<std::vector<int>>& neighborVec)
{
	polyhedron.resize(positionVec.size());

	for (int i = 0; i < positionVec.size(); i++)
	{
		polyhedron[i].Position = positionVec[i];
		polyhedron[i].NeighborVertexVec = neighborVec[i];
	}
}

void Poly::Moments(double& zerothMoment, Vector3& firstMoment, const Polyhedron& polyhedron)
{
	// Clear the result for accumulation.
	zerothMoment = 0.0;
	firstMoment = Vector3(0.0, 0.0, 0.0);

	if (polyhedron.size() > 3)
	{
		const auto origin = polyhedron[0].Position;

		// Walk the facets
		const auto facets = ExtractFaces(polyhedron);
		for (const auto& facet : facets)
		{
			const auto n = facet.size();
			const auto p0 = polyhedron[facet[0]].Position - origin;
			for (auto k = 1u; k < n - 1; ++k)
			{
				const auto i = facet[k];
				const auto j = facet[(k + 1) % n];
				const auto p1 = polyhedron[i].Position - origin;
				const auto p2 = polyhedron[j].Position - origin;
				const auto dV = p0.Dot(p1.Cross(p2));
				zerothMoment += dV;
				firstMoment += (p0 + p1 + p2) * dV;
			}
		}

		zerothMoment /= 6.0;
		firstMoment *= safeInv(24.0 * zerothMoment);
		firstMoment += origin;
	}
}

std::vector<std::vector<int>> Poly::ExtractFaces(const Polyhedron& polyhedron)
{
	std::vector<std::vector<int>> faceVertices;
	std::set<std::pair<int, int>> visitedEdge;

	for (int i = 0; i < polyhedron.size(); i++)
	{
		const Vertex& vert = polyhedron[i];
		if (vert.comp >= 0)
		{
			for (const int adjIndex : vert.NeighborVertexVec)
			{
				if (visitedEdge.end() != visitedEdge.find(std::make_pair(i, adjIndex)))
					continue;

				std::vector<int> face(1, i);
				int istart = i;
				int iprev = i;
				int inext = adjIndex;
				int itmp = adjIndex;

				while (inext != istart)
				{
					visitedEdge.insert(std::make_pair(iprev, inext));
					face.push_back(inext);
					itmp = inext;
					inext = FaceLoop(polyhedron[inext], iprev);
					iprev = itmp;
				}

				visitedEdge.insert(std::make_pair(iprev, inext)); // Final edge connecting last->first vertex
				faceVertices.push_back(face);
			}
		}
	}

	return faceVertices;
}

std::vector<std::vector<int>> Poly::ExtractNeighborFromMesh(std::vector<Vector3>& vertices, std::vector<int>& indices)
{
	std::vector<VMACH::Triangle> triVec;
	std::unordered_map<int, std::vector<int>> vertexAdjTriIndex;
	for (int i = 0; i < indices.size(); i += 3)
	{
		VMACH::Triangle tri;
		tri.VertexVec = { (int)indices[i], (int)indices[i + 1], (int)indices[i + 2] };

		tri.Normal = (vertices[tri.VertexVec[1]] - vertices[tri.VertexVec[0]])
			.Cross(vertices[tri.VertexVec[2]] - vertices[tri.VertexVec[0]]);
		tri.Normal.Normalize();

		triVec.push_back(tri);
		int triIndex = triVec.size() - 1;

		vertexAdjTriIndex[indices[i]].push_back(triIndex);
		vertexAdjTriIndex[indices[i + 1]].push_back(triIndex);
		vertexAdjTriIndex[indices[i + 2]].push_back(triIndex);
	}

	for (int t = 0; t < triVec.size(); t++)
	{
		VMACH::Triangle& tri = triVec[t];

		for (int v = 0; v < tri.VertexVec.size(); v++)
		{
			auto& vec1 = vertexAdjTriIndex[tri.VertexVec[v]];
			auto& vec2 = vertexAdjTriIndex[tri.VertexVec[(v + 1) % tri.VertexVec.size()]];

			std::vector<int> intersection;
			std::set_intersection(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(),
								  std::back_inserter(intersection));

			for (int i = 0; i < intersection.size(); i++)
			{
				if (intersection[i] == t)
					continue;

				if (tri.AdjTriangleVec.end() !=
					std::find(tri.AdjTriangleVec.begin(), tri.AdjTriangleVec.end(), intersection[i]))
					continue;

				tri.AdjTriangleVec.push_back(intersection[i]);
			}
		}
	}

	std::vector<std::vector<int>> nei(vertices.size());
	for (auto& [iVert, adjVec] : vertexAdjTriIndex)
	{
		Vector3 vertNormal;
		for (const int& t : adjVec)
			vertNormal += triVec[t].Normal;
		vertNormal /= adjVec.size();

		std::vector<int> tail = { adjVec[0] };
		int curr = adjVec[0];
		while (TRUE)
		{
			std::vector<int> candidate;
			std::copy_if(
				triVec[curr].AdjTriangleVec.begin(),
				triVec[curr].AdjTriangleVec.end(),
				std::back_inserter(candidate),
				[&](const int& t)
				{
					bool visited = tail.end() != std::find(tail.begin(), tail.end(), t);
					if (TRUE == visited)
						return false;

					return triVec[t].VertexVec.end() !=
						std::find(triVec[t].VertexVec.begin(), triVec[t].VertexVec.end(), iVert);
				});

			if (candidate.size() == 0)
				break;

			if (candidate.size() == 1)
			{
				tail.push_back(candidate[0]);
				curr = candidate[0];
			}
			else if (candidate.size() == 2)
			{
				tail.push_back(candidate[0]);
				curr = candidate[0];
			}
		}

		std::vector<int> collection;
		for (int i = 0; i < tail.size(); i++)
		{
			int startOffset = 0;
			for (int v = 0; v < triVec[tail[i]].VertexVec.size(); v++)
			{
				if (triVec[tail[i]].VertexVec[v] == iVert)
				{
					startOffset = v;
					break;
				}
			}

			collection.push_back(triVec[tail[i]].VertexVec[(startOffset + 1) % 3]);
			collection.push_back(triVec[tail[i]].VertexVec[(startOffset + 2) % 3]);
		}

		if (collection.size() >= 3)
		{
			bool isCCW = collection[1] != collection[2];
			if (TRUE == isCCW)
				for (int i = 0; i < collection.size(); i += 2)
					std::swap(collection[i], collection[i + 1]);

			std::vector<int> uniqueCollection;
			UniqueVector(collection, uniqueCollection);
			collection = uniqueCollection;

			if (TRUE == isCCW)
				std::reverse(collection.begin(), collection.end());
		}

		nei[iVert] = collection;
	}

	for (int v = 0; v < nei.size(); v++)
	{
		for (int j = 0; j < nei[v].size(); j++)
		{
			if (nei[nei[v][j]].end() == std::find(nei[nei[v][j]].begin(), nei[nei[v][j]].end(), v))
				throw std::exception();
		}
	}

	return nei;
}

void Poly::ClipPolyhedron(Polyhedron& polyhedron, const std::vector<Plane>& planes)
{
	bool updated;
	int nverts0, nverts, nneigh, i, ii, j, k, jn, inew, iprev, inext, itmp;
	std::vector<int>::iterator nitr;
	const double nearlyZero = 1.0e-15;

	// Prepare to dump the input state if we hit an exception
	std::string initial_state;

	// Find the bounding box of the polyhedron.
	auto xmin = std::numeric_limits<double>::max(), xmax = std::numeric_limits<double>::lowest();
	auto ymin = std::numeric_limits<double>::max(), ymax = std::numeric_limits<double>::lowest();
	auto zmin = std::numeric_limits<double>::max(), zmax = std::numeric_limits<double>::lowest();
	for (auto& v : polyhedron)
	{
		xmin = std::min(xmin, (double)v.Position.x);
		xmax = std::max(xmax, (double)v.Position.x);
		ymin = std::min(ymin, (double)v.Position.y);
		ymax = std::max(ymax, (double)v.Position.y);
		zmin = std::min(zmin, (double)v.Position.z);
		zmax = std::max(zmax, (double)v.Position.z);
	}

	// Loop over the planes.
	auto kplane = 0u;
	const auto nplanes = planes.size();
	while (kplane < nplanes && !polyhedron.empty())
	{
		const auto& plane = planes[kplane++];

		// First check against the bounding box.
		auto boxcomp = ComparePlaneBB(plane, xmin, ymin, zmin, xmax, ymax, zmax);
		auto above = boxcomp == 1;
		auto below = boxcomp == -1;

		// Check the current set of vertices against this plane.
		// Also keep track of any vertices that landed exactly in-plane.
		if (!(above || below))
		{
			above = true;
			below = true;
			for (auto& v : polyhedron)
			{
				v.comp = ComparePlanePoint(plane, v.Position);
				if (v.comp == 1)
				{
					below = false;
				}
				else if (v.comp == -1)
				{
					above = false;
				}
			}
		}

		// Did we get a simple case?
		if (below)
		{
			// The polyhedron is entirely below the clip plane, && is therefore entirely removed.
			// No need to check any more clipping planes -- we're done.
			polyhedron.clear();
		}
		else if (!above)
		{
			// This plane passes through the polyhedron.
			// Insert any new vertices.
			nverts0 = polyhedron.size();
			for (i = 0; i < nverts0; ++i)
			{ // Only check vertices before we start adding new ones.
				if (polyhedron[i].comp == -1)
				{
					// This vertex is clipped, scan it's neighbors for any that survive
					nneigh = polyhedron[i].NeighborVertexVec.size();
					for (auto j = 0; j < nneigh; ++j)
					{
						jn = polyhedron[i].NeighborVertexVec[j];
						if (polyhedron[jn].comp > 0)
						{
							// This edge straddles the clip plane, so insert a new vertex.
							inew = polyhedron.size();
							polyhedron.push_back(
								Vertex(PlaneLineIntersection(polyhedron[i].Position, polyhedron[jn].Position, plane), 2)); // 2 indicates new vertex
							polyhedron[inew].NeighborVertexVec = std::vector<int>({ i, jn });

							nitr = find(polyhedron[jn].NeighborVertexVec.begin(),
										polyhedron[jn].NeighborVertexVec.end(), i);

							*nitr = inew;
							polyhedron[i].NeighborVertexVec[j] = inew;
						}
					}
				}
				else if (polyhedron[i].comp == 0)
				{
					// This vertex is exactly in plane.
				}
			}
			nverts = polyhedron.size();

			// Look for any topology links to clipped nodes we need to patch.
			// We hit any new vertices first, && then any preexisting that happened to lie exactly in-plane.
			std::vector<std::vector<int>> old_neighbors(nverts);
			for (i = 0; i < nverts; ++i)
				old_neighbors[i] = polyhedron[i].NeighborVertexVec;
			for (ii = 0; ii < nverts; ++ii)
			{
				i = (ii + nverts0) % nverts;
				if (polyhedron[i].comp == 0 || polyhedron[i].comp == 2)
				{
					nneigh = polyhedron[i].NeighborVertexVec.size();

					// Look for any neighbors of the vertex that are clipped.
					for (j = 0; j < nneigh; ++j)
					{
						jn = polyhedron[i].NeighborVertexVec[j];
						if (polyhedron[jn].comp == -1)
						{
							// This neighbor is clipped, so look for the first unclipped vertex along this face loop.
							iprev = i;
							inext = jn;
							itmp = inext;

							k = 0;
							while (polyhedron[inext].comp == -1 && k++ < nverts)
							{
								itmp = inext;
								inext = FaceLoop(polyhedron[inext], iprev);
								iprev = itmp;
							}

							if (polyhedron[i].NeighborVertexVec[(j + 1u) % polyhedron[i].NeighborVertexVec.size()] ==
									inext ||
								inext == i)
							{
								polyhedron[i].NeighborVertexVec[j] = -1; // mark to be removed
							}
							else
							{
								polyhedron[i].NeighborVertexVec[j] = inext;
								if (polyhedron[inext].comp == 2)
								{
									polyhedron[inext].NeighborVertexVec.insert(
										polyhedron[inext].NeighborVertexVec.begin(), i);
									old_neighbors[inext].insert(old_neighbors[inext].begin(), -1);
								}
								else
								{
									size_t offset = std::distance(
										old_neighbors[inext].begin(),
										std::find(old_neighbors[inext].begin(), old_neighbors[inext].end(), iprev));

									polyhedron[inext].NeighborVertexVec.insert(
										polyhedron[inext].NeighborVertexVec.begin() + offset, i);
									old_neighbors[inext].insert(old_neighbors[inext].begin() + offset, i);
								}
							}
						}
					}
				}
			}
			for (i = 0; i < nverts; ++i)
			{
				polyhedron[i].NeighborVertexVec.erase(
					std::remove(polyhedron[i].NeighborVertexVec.begin(), polyhedron[i].NeighborVertexVec.end(), -1),
					polyhedron[i].NeighborVertexVec.end());
			}

			// Check for any points with just two neighbors that are colinear
			updated = true;
			while (updated)
			{
				updated = false;
				for (i = 0; i < nverts; ++i)
				{
					auto& v = polyhedron[i];
					if (v.comp >= 0 && v.NeighborVertexVec.size() == 2)
					{
						updated = true;
						iprev = v.NeighborVertexVec[0];
						inext = v.NeighborVertexVec[1];
						auto& vprev = polyhedron[iprev];
						auto& vnext = polyhedron[inext];

						k = 0;
						while (k < vprev.NeighborVertexVec.size() && vprev.NeighborVertexVec[k] != i)
							++k;

						vprev.NeighborVertexVec[k] = inext;
						k = 0;
						while (k < vnext.NeighborVertexVec.size() && vnext.NeighborVertexVec[k] != i)
							++k;

						vnext.NeighborVertexVec[k] = iprev;
						v.comp = -1; // Mark this vertex for removal
					}
				}
			}

			// Remove the clipped vertices && collapse degenerates, compressing the polyhedron.
			i = 0;
			xmin = std::numeric_limits<double>::max(), xmax = std::numeric_limits<double>::lowest();
			ymin = std::numeric_limits<double>::max(), ymax = std::numeric_limits<double>::lowest();
			zmin = std::numeric_limits<double>::max(), zmax = std::numeric_limits<double>::lowest();
			for (auto& v : polyhedron)
			{
				if (v.comp >= 0)
				{
					v.ID = i++;
					xmin = std::min(xmin, (double)v.Position.x);
					xmax = std::max(xmax, (double)v.Position.x);
					ymin = std::min(ymin, (double)v.Position.y);
					ymax = std::max(ymax, (double)v.Position.y);
					zmin = std::min(zmin, (double)v.Position.z);
					zmax = std::max(zmax, (double)v.Position.z);
				}
			}

			// Renumber the neighbor links.
			for (i = 0; i < nverts; ++i)
			{
				if (polyhedron[i].comp >= 0)
				{
					for (j = 0; j < int(polyhedron[i].NeighborVertexVec.size()); ++j)
					{
						polyhedron[i].NeighborVertexVec[j] = polyhedron[polyhedron[i].NeighborVertexVec[j]].ID;
					}
				}
			}
			polyhedron.erase(std::remove_if(polyhedron.begin(), polyhedron.end(), [](Vertex& v) { return v.comp < 0; }),
							 polyhedron.end());

			// Is the polyhedron gone?
			if (polyhedron.size() < 4)
				polyhedron.clear();
		}

		if (FALSE)
		{
			auto faces = ExtractFaces(polyhedron);

			std::vector<std::vector<int>> maintainFaces;
			std::vector<std::vector<int>> capFaces;
			for (const std::vector<int>& face : faces)
			{
				if (face.size() > 3)
					capFaces.push_back(face);
				else
					maintainFaces.push_back(face);
			}

			for (const std::vector<int>& face : capFaces)
			{
				VMACH::PolygonFace polyFace = { false };
				for (const int iVert : face)
					polyFace.AddVertex(polyhedron[iVert].Position);

				const Vector3 a = polyhedron[face[0]].Position;
				const Vector3 b = polyhedron[face[1]].Position;
				const Vector3 c = polyhedron[face[2]].Position;

				Vector3 assumeNormal = (b - a).Cross(c - a);
				assumeNormal.Normalize();

				if (TRUE == polyFace.IsCCW(assumeNormal))
					assumeNormal = -assumeNormal;

				polyFace.ManuallySetFacePlane(DirectX::SimpleMath::Plane(a, assumeNormal));

				std::vector<int> localTriangulated = polyFace.EarClipping();
				std::vector<int> triangulated;
				for (const int local : localTriangulated)
					triangulated.push_back(face[local]);

				for (int i = 0; i < triangulated.size(); i += 3)
					maintainFaces.push_back({ triangulated[i], triangulated[i + 1], triangulated[i + 2] });
			}

			std::vector<int> flat;
			for (const std::vector<int>& face : maintainFaces)
				flat.insert(flat.end(), face.begin(), face.end());

			std::vector<Vector3> vertices(polyhedron.size());
			std::transform(polyhedron.begin(), polyhedron.end(), vertices.begin(), [](const Vertex& vert) { return vert.Position; });

			std::vector<std::vector<int>> nei = ExtractNeighborFromMesh(vertices, flat);
			Poly::InitPolyhedron(polyhedron, vertices, nei);
		}
	}
}

Poly::Polyhedron Poly::ClipPolyhedron(const Polyhedron& polyhedron, const VMACH::Polygon3D& polygon3D)
{
	std::vector<Plane> planes;
	for (const auto& f : polygon3D.FaceVec)
		planes.push_back(f.FacePlane);

	Polyhedron res = polyhedron;
	ClipPolyhedron(res, planes);

	return res;
}

void Poly::Translate(Polyhedron& polyhedron, const Vector3& v)
{
	for (auto& i : polyhedron)
		i.Position += v;
}

void Poly::Scale(Polyhedron& polyhedron, const Vector3& v)
{
	for (auto& i : polyhedron)
		i.Position *= v;
}

void Poly::Transform(Polyhedron& polyhedron, const DirectX::XMMATRIX& matrix)
{
	const DirectX::XMMATRIX mat = XMMatrixTranspose(matrix);
	for (Poly::Vertex& vert : polyhedron)
		vert.Position = XMVector3TransformCoord(vert.Position, mat);
}

Poly::Polyhedron Poly::GetBB()
{
	std::vector<Vector3> points =
	{
		Vector3(-0.5,-0.5,-0.5),
		Vector3(+0.5,-0.5,-0.5),
		Vector3(+0.5,+0.5,-0.5),
		Vector3(-0.5,+0.5,-0.5),
		Vector3(-0.5,-0.5,+0.5),
		Vector3(+0.5,-0.5,+0.5),
		Vector3(+0.5,+0.5,+0.5),
		Vector3(-0.5,+0.5,+0.5)
	};

	const std::vector<std::vector<int>> neighbors =
	{
		{1, 4, 3},
		{5, 0, 2},
		{3, 6, 1},
		{7, 2, 0},
		{5, 7, 0},
		{1, 6, 4},
		{5, 2, 7},
		{4, 6, 3}
	};

	std::vector<Poly::Vertex> poly;
	Poly::InitPolyhedron(poly, points, neighbors);

	return poly;
}

void Poly::RenderPolyhedron(std::vector<VertexNormalColor>& vertexData, 
							std::vector<uint32_t>& indexData, 
							const Polyhedron& poly, 
							const std::vector<std::vector<int>>& extract, 
							bool isConvex, 
							Vector3 color)
{
	const size_t vertexOffset = vertexData.size();

	vertexData.resize(vertexOffset + poly.size());
	std::transform(poly.begin(), 
				   poly.end(), 
				   std::next(vertexData.begin(), vertexOffset), 
				   [&color](const Poly::Vertex& vert) { return VertexNormalColor(vert.Position, DirectX::XMFLOAT3(), color); });

	if (TRUE == isConvex)
	{
		for (const auto& f : extract)
		{
			for (int v = 1; v < f.size() - 1; v++)
			{
				indexData.push_back(vertexOffset + f[0]);
				indexData.push_back(vertexOffset + f[v]);
				indexData.push_back(vertexOffset + f[v + 1]);
			}
		}
	}
	else
	{
		for (const auto& f : extract)
			for (const int v : EarClipping(poly, f))
				indexData.push_back(vertexOffset + f[v]);
	}
}

int Poly::ComparePlanePoint(const Plane& plane, const Vector3& point)
{
	const auto sgndist = plane.D() + plane.Normal().Dot(point);
	if (std::abs(sgndist) < 1.0e-10)
		return 0;

	return sgn0(-sgndist);
}

int Poly::ComparePlaneBB(const Plane& plane, const double xmin, const double ymin, const double zmin, const double xmax, const double ymax, const double zmax)
{
	const auto c1 = ComparePlanePoint(plane, Vector3(xmin, ymin, zmin));
	const auto c2 = ComparePlanePoint(plane, Vector3(xmax, ymin, zmin));
	const auto c3 = ComparePlanePoint(plane, Vector3(xmax, ymax, zmin));
	const auto c4 = ComparePlanePoint(plane, Vector3(xmin, ymax, zmin));
	const auto c5 = ComparePlanePoint(plane, Vector3(xmin, ymin, zmax));
	const auto c6 = ComparePlanePoint(plane, Vector3(xmax, ymin, zmax));
	const auto c7 = ComparePlanePoint(plane, Vector3(xmax, ymax, zmax));
	const auto c8 = ComparePlanePoint(plane, Vector3(xmin, ymax, zmax));
	const auto cmin = std::min(c1, std::min(c2, std::min(c3, std::min(c4, std::min(c5, std::min(c6, std::min(c7, c8)))))));
	const auto cmax = std::max(c1, std::max(c2, std::max(c3, std::max(c4, std::max(c5, std::max(c6, std::max(c7, c8)))))));

	if (cmin >= 0)
		return 1;
	else if (cmax <= 0)
		return -1;
	else
		return 0;
}

Poly::Vector3 Poly::PlaneLineIntersection(const typename Vector3& a, const typename Vector3& b, const Plane& plane)
{
	const auto asgndist = plane.D() + plane.Normal().Dot(a);
	const auto bsgndist = plane.D() + plane.Normal().Dot(b);
	return ((a * bsgndist) - (b * asgndist)) / (bsgndist - asgndist);
}

bool Poly::IsCCW(const Polyhedron& polyhedron, const std::vector<int>& face, const Vector3& normal)
{
	Vector3 P = polyhedron[face[0]].Position;
	Vector3 S;

	for (int v = 0; v < face.size(); v++)
		S += (polyhedron[face[v]].Position - P).Cross(polyhedron[face[(v + 1) % face.size()]].Position - P);

	return S.Dot(normal) < 0;
}

std::vector<int> Poly::EarClipping(const Polyhedron& polyhedron, const std::vector<int>& face)
{
	struct VertexNode
	{
		VertexNode* Prev;
		VertexNode* Next;
		int         Index;
		bool        IsReflex;
	};

	const int N = face.size();

	const Vector3 a = polyhedron[face[0]].Position;
	const Vector3 b = polyhedron[face[1]].Position;
	const Vector3 c = polyhedron[face[2]].Position;
	Vector3 normal = (b - a).Cross(c - a);
	if (TRUE == IsCCW(polyhedron, face, normal))
		normal = -normal;

	std::vector<int> triangles;

	if (N <= 2)
		return triangles;

	if (N == 3)
	{
		triangles.resize(3);
		triangles = { 0, 1, 2 };
		return triangles;
	}

	std::vector<VertexNode> vertices(N);

	const auto isReflex = [&](const VertexNode& vertex)
	{
		const Vector3& a = polyhedron[face[vertex.Prev->Index]].Position;
		const Vector3& b = polyhedron[face[vertex.Index]].Position;
		const Vector3& c = polyhedron[face[vertex.Next->Index]].Position;

		return !VMACH::OnYourRight(a, b, c, normal);
	};

	const auto initVertex = [&](const int& curIndex, const int& prevIndex, const int& nextIndex)
	{
		vertices[curIndex].Index = curIndex;
		vertices[curIndex].Prev = &vertices[prevIndex];
		vertices[curIndex].Next = &vertices[nextIndex];
		vertices[curIndex].Prev->Index = prevIndex;
		vertices[curIndex].Next->Index = nextIndex;
		vertices[curIndex].IsReflex = isReflex(vertices[curIndex]);
	};

	// Initialize vertices.
	initVertex(0, N - 1, 1);
	initVertex(N - 1, N - 2, 0);

	for (int i = 1; i < N - 1; i++)
		initVertex(i, i - 1, i + 1);

	// Initialize reflex vertices.
	std::list<VertexNode*> reflexVertices;
	for (int i = 0; i < N; i++)
		if (TRUE == vertices[i].IsReflex)
			reflexVertices.push_back(&vertices[i]);

	const auto isEar = [&](const VertexNode& vertex)
	{
		if (TRUE == vertex.IsReflex)
			return false;

		const Vector3& a = polyhedron[face[vertex.Prev->Index]].Position;
		const Vector3& b = polyhedron[face[vertex.Index]].Position;
		const Vector3& c = polyhedron[face[vertex.Next->Index]].Position;

		for (auto itr = reflexVertices.begin(); itr != reflexVertices.end(); itr++)
		{
			int index = (*itr)->Index;

			if (index == vertex.Prev->Index || index == vertex.Next->Index)
				continue;

			if (polyhedron[face[index]].Position == a || polyhedron[face[index]].Position == b || polyhedron[face[index]] == c)
				continue;

			// Check any point is inside or not.
			if (FALSE == VMACH::OnYourRight(a, b, polyhedron[face[index]].Position, normal))
				continue;
			if (FALSE == VMACH::OnYourRight(b, c, polyhedron[face[index]].Position, normal))
				continue;
			if (FALSE == VMACH::OnYourRight(c, a, polyhedron[face[index]].Position, normal))
				continue;

			return false;
		}

		return true;
	};

	// Do triangulation.
	triangles.resize(3 * (N - 2));

	int skipped = 0;
	int triangleIndex = 0;
	int nVertices = vertices.size();

	VertexNode* current = &vertices[0];
	while (nVertices > 3)
	{
		VertexNode* prev = current->Prev;
		VertexNode* next = current->Next;

		if (TRUE == isEar(*current))
		{
			triangles[triangleIndex + 0] = prev->Index;
			triangles[triangleIndex + 1] = current->Index;
			triangles[triangleIndex + 2] = next->Index;

			prev->Next = next;
			next->Prev = prev;

			VertexNode* adjacent[2] = { prev, next };

			for (int i = 0; i < 2; i++)
			{
				if (FALSE == adjacent[i]->IsReflex)
					continue;

				adjacent[i]->IsReflex = isReflex(*adjacent[i]);
				if (FALSE == adjacent[i]->IsReflex)
					reflexVertices.remove(adjacent[i]);
			}

			triangleIndex += 3;
			nVertices--;
			skipped = 0;
		}
		else if (++skipped > nVertices)
		{
			triangles.clear();
			return triangles;
		}

		current = next;
	}

	triangles[triangleIndex + 0] = current->Prev->Index;
	triangles[triangleIndex + 1] = current->Index;
	triangles[triangleIndex + 2] = current->Next->Index;

	return triangles;
}
