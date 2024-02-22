#include "pch.h"
#include "Poly.h"

#include "VMACH.h"

Poly::Plane::Plane(const Plane &rhs) : D(rhs.D), Normal(rhs.Normal), ID(rhs.ID) {}

Poly::Plane::Plane() : D(0.0), Normal(Vector3(1.0, 0.0, 0.0)), ID(0) {}
Poly::Plane::Plane(const double d, const Vector3 &normal) : D(d), Normal(normal), ID(0) {}
Poly::Plane::Plane(const double d, const Vector3 &normal, const size_t id) : D(d), Normal(normal), ID(id) {}
Poly::Plane::Plane(const Vector3 &p, const Vector3 &normal) : D(normal.Dot(-p)), Normal(normal), ID(0) {}
Poly::Plane::Plane(const Vector3 &p, const Vector3 &normal, const size_t id) : D(normal.Dot(-p)), Normal(normal), ID(id) {}
Poly::Vertex::Vertex(const Vertex &rhs)
    : Position(rhs.Position), NeighborVertexVec(rhs.NeighborVertexVec), comp(rhs.comp), ID(rhs.ID), clips(rhs.clips)
{}

Poly::Plane &Poly::Plane::operator=(const Plane &rhs)
{
    D = rhs.D;
    Normal = rhs.Normal;
    ID = rhs.ID;

    return *this;
}

bool Poly::Plane::operator==(const Plane &rhs) const { return (D == rhs.D && Normal == rhs.Normal); }
bool Poly::Plane::operator!=(const Plane &rhs) const { return *this != rhs; }
bool Poly::Plane::operator<(const Plane &rhs) const { return (D < rhs.D); }
bool Poly::Plane::operator>(const Plane &rhs) const { return (D > rhs.D); }

Poly::Vertex::Vertex() : Position(Vector3(0, 0, 0)), NeighborVertexVec(), comp(1), ID(-1), clips() {}
Poly::Vertex::Vertex(const Vector3 &pos) : Position(pos), NeighborVertexVec(), comp(1), ID(-1), clips() {}
Poly::Vertex::Vertex(const Vector3 &pos, const int c) : Position(pos), NeighborVertexVec(), comp(c), ID(-1), clips() {}

Poly::Vertex &Poly::Vertex::operator=(const Vertex &rhs)
{
    Position = rhs.Position;
    NeighborVertexVec = rhs.NeighborVertexVec;
    comp = rhs.comp;
    ID = rhs.ID;
    clips = rhs.clips;

    return *this;
}

bool Poly::Vertex::operator==(const Vertex &rhs) const
{
    return (Position == rhs.Position && NeighborVertexVec == rhs.NeighborVertexVec && comp == rhs.comp && ID == rhs.ID);
}

double sgn(const double x) { return (x >= 0.0 ? 1.0 : -1.0); }
double sgn0(const double x) { return (x > 0.0 ? 1.0 : x < 0.0 ? -1.0 : 0.0); }
template <typename Value> Value safeInv(const Value &x, const double fuzz = 1.0e-30)
{
    return sgn(x) / std::max(fuzz, std::abs(x));
}

int Poly::nextInFaceLoop(const Vertex &v, const int vprev)
{
    const auto itr = find(v.NeighborVertexVec.begin(), v.NeighborVertexVec.end(), vprev);
    if (itr == v.NeighborVertexVec.begin())
    {
        return v.NeighborVertexVec.back();
    }
    else
    {
        return *(itr - 1);
    }
}

void Poly::InitPolyhedron(Polyhedron &polyhedron, const std::vector<Vector3> &positionVec,
                          const std::vector<std::vector<int>> &neighborVec)
{
    polyhedron.resize(positionVec.size());

    for (int i = 0; i < positionVec.size(); i++)
    {
        polyhedron[i].Position = positionVec[i];
        polyhedron[i].NeighborVertexVec = neighborVec[i];
    }
}

std::vector<std::vector<int>> Poly::ExtractFaces(const Polyhedron &polyhedron)
{
    std::vector<std::vector<int>> faceVertices;
    std::set<std::pair<int, int>> visitedEdge;

    for (int i = 0; i < polyhedron.size(); i++)
    {
        const Vertex &vert = polyhedron[i];
        if (vert.comp >= 0)
        {
            for (const int adjIndex : vert.NeighborVertexVec)
            {
                if (visitedEdge.end() != visitedEdge.find(make_pair(i, adjIndex)))
                    continue;

                std::vector<int> face(1, i);
                int istart = i;
                int iprev = i;
                int inext = adjIndex;
                int itmp = adjIndex;

                while (inext != istart)
                {
                    visitedEdge.insert(make_pair(iprev, inext));
                    face.push_back(inext);
                    itmp = inext;
                    inext = nextInFaceLoop(polyhedron[inext], iprev);
                    iprev = itmp;
                }

                visitedEdge.insert(make_pair(iprev, inext)); // Final edge connecting last->first vertex
                faceVertices.push_back(face);
            }
        }
    }

    return faceVertices;
}

void Poly::Moments(double &zerothMoment, Vector3 &firstMoment, const std::vector<Vertex> &polyhedron)
{
    // Clear the result for accumulation.
    zerothMoment = 0.0;
    firstMoment = Vector3(0.0, 0.0, 0.0);

    if (polyhedron.size() > 3)
    {
        const auto origin = polyhedron[0].Position;

        // Walk the facets
        const auto facets = ExtractFaces(polyhedron);
        for (const auto &facet : facets)
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

int Poly::Compare(const Plane &plane, const Vector3 &point)
{
    const auto sgndist = plane.D + plane.Normal.Dot(point);
    if (std::abs(sgndist) < 1.0e-10)
        return 0;

    return sgn0(sgndist);
}

Poly::Vector3 Poly::segmentPlaneIntersection(const typename Vector3 &a, const typename Vector3 &b, const Plane &plane)
{
    const auto asgndist = plane.D + plane.Normal.Dot(a);
    const auto bsgndist = plane.D + plane.Normal.Dot(b);
    return ((a * bsgndist) - (b * asgndist)) / (bsgndist - asgndist);
}

void Poly::ClipPolyhedron(std::vector<Vertex> &polyhedron, const std::vector<Plane> &planes)
{
    bool updated;
    int nverts0, nverts, nneigh, i, ii, j, k, jn, inew, iprev, inext, itmp;
    vector<int>::iterator nitr;
    const double nearlyZero = 1.0e-15;

    // Prepare to dump the input state if we hit an exception
    std::string initial_state;

    // Check the input.
    /*double V0;
    Vector3 C0;
    Moments(V0, C0, polyhedron);
    if (V0 < nearlyZero)
        polyhedron.clear();*/

    // Find the bounding box of the polyhedron.
    auto xmin = std::numeric_limits<double>::max(), xmax = std::numeric_limits<double>::lowest();
    auto ymin = std::numeric_limits<double>::max(), ymax = std::numeric_limits<double>::lowest();
    auto zmin = std::numeric_limits<double>::max(), zmax = std::numeric_limits<double>::lowest();
    for (auto &v : polyhedron)
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
        const auto &plane = planes[kplane++];

        // First check against the bounding box.
        auto boxcomp = Compare(plane, xmin, ymin, zmin, xmax, ymax, zmax);
        auto above = boxcomp == 1;
        auto below = boxcomp == -1;

        // Check the current set of vertices against this plane.
        // Also keep track of any vertices that landed exactly in-plane.
        if (!(above || below))
        {
            above = true;
            below = true;
            for (auto &v : polyhedron)
            {
                v.comp = Compare(plane, v.Position);
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
                                Vertex(segmentPlaneIntersection(polyhedron[i].Position, polyhedron[jn].Position, plane),
                                       2)); // 2 indicates new vertex
                            // PCASSERT2(polyhedron.size() == inew + 1u, internal::dumpSerializedState(initial_state));
                            polyhedron[inew].NeighborVertexVec = vector<int>({i, jn});
                            polyhedron[inew].clips.insert(plane.ID);

                            // Patch up clip info -- gotta scan for common elements in the neighbors of the clipped
                            // guy.
                            std::set<int> common_clips;
                            std::set_intersection(polyhedron[i].clips.begin(), polyhedron[i].clips.end(),
                                                  polyhedron[jn].clips.begin(), polyhedron[jn].clips.end(),
                                                  std::inserter(common_clips, common_clips.begin()));
                            polyhedron[inew].clips.insert(common_clips.begin(), common_clips.end());

                            nitr = find(polyhedron[jn].NeighborVertexVec.begin(),
                                        polyhedron[jn].NeighborVertexVec.end(), i);
                            // PCASSERT2(nitr != polyhedron[jn].neighbors.end(),
                            // internal::dumpSerializedState(initial_state));
                            *nitr = inew;
                            polyhedron[i].NeighborVertexVec[j] = inew;
                        }
                    }
                }
                else if (polyhedron[i].comp == 0)
                {
                    // This vertex is exactly in plane, so we add this plane as a clip of the vertex.
                    polyhedron[i].clips.insert(plane.ID);
                }
            }
            nverts = polyhedron.size();

            // Look for any topology links to clipped nodes we need to patch.
            // We hit any new vertices first, && then any preexisting that happened to lie exactly in-plane.
            vector<vector<int>> old_neighbors(nverts);
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
                                inext = nextInFaceLoop(polyhedron[inext], iprev);
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
                    auto &v = polyhedron[i];
                    if (v.comp >= 0 && v.NeighborVertexVec.size() == 2)
                    {
                        updated = true;
                        iprev = v.NeighborVertexVec[0];
                        inext = v.NeighborVertexVec[1];
                        auto &vprev = polyhedron[iprev];
                        auto &vnext = polyhedron[inext];

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
            for (auto &v : polyhedron)
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
            polyhedron.erase(std::remove_if(polyhedron.begin(), polyhedron.end(), [](Vertex &v) { return v.comp < 0; }),
                             polyhedron.end());

            // Is the polyhedron gone?
            if (polyhedron.size() < 4)
            {
                polyhedron.clear();
            }
            else
            {
                /*double V1;
                Vector3 C1;
                Moments(V1, C1, polyhedron);

                if (V1 < nearlyZero || V1 / V0 < 100.0 * nearlyZero)
                    polyhedron.clear();*/
            }
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

void Poly::collapseDegenerates(std::vector<Vertex>& polyhedron, const double tol)
{
	// Prepare to dump the input state if we hit an exception
	std::string initial_state;

	const auto tol2 = tol * tol;
	auto n = polyhedron.size();
	if (n > 0)
	{

		// Set the initial ID's the vertices.
		for (auto i = 0u; i < n; ++i) polyhedron[i].ID = i;

		// cerr << "Initial: " << endl << polyhedron2string(polyhedron) << endl;

		// Walk the polyhedron removing degenerate edges until we make a sweep without
		// removing any.  Don't worry about ordering of the neighbors yet.
		auto active = false;
		for (auto i = 0u; i < n; ++i)
		{
			if (polyhedron[i].ID >= 0)
			{
				auto idone = false;
				while (not idone)
				{
					idone = true;
					for (auto jneigh = 0u; jneigh < polyhedron[i].NeighborVertexVec.size(); ++jneigh)
					{
						const size_t j = polyhedron[i].NeighborVertexVec[jneigh];
						if ((polyhedron[i].Position - polyhedron[j].Position).LengthSquared() < tol2)
						{
							// cerr << " --> collapasing " << j << " to " << i;
							active = true;
							idone = false;
							polyhedron[j].ID = -1;
							polyhedron[i].clips.insert(polyhedron[j].clips.begin(), polyhedron[j].clips.end());

							// Merge the NeighborVertexVec of j->i.
							auto jitr = polyhedron[i].NeighborVertexVec.begin() + jneigh;
							auto kitr = find(polyhedron[j].NeighborVertexVec.begin(), polyhedron[j].NeighborVertexVec.end(), i);
							jitr = polyhedron[i].NeighborVertexVec.insert(jitr, polyhedron[j].NeighborVertexVec.begin(), kitr);
							jitr = polyhedron[i].NeighborVertexVec.insert(jitr, kitr + 1, polyhedron[j].NeighborVertexVec.end());  // jitr now points at the first of the newly inserted NeighborVertexVec

							// Make sure i & j are removed from the neighbor set of i.
							polyhedron[i].NeighborVertexVec.erase(remove_if(polyhedron[i].NeighborVertexVec.begin(), polyhedron[i].NeighborVertexVec.end(),
								[&](const size_t val) { return val == i or val == j; }),
														  polyhedron[i].NeighborVertexVec.end());

							// Remove any adjacent repeats.
							for (auto kitr = polyhedron[i].NeighborVertexVec.begin(); kitr < polyhedron[i].NeighborVertexVec.end() - 1; ++kitr)
							{
								if (*kitr == *(kitr + 1)) kitr = polyhedron[i].NeighborVertexVec.erase(kitr);
							}
							if (polyhedron[i].NeighborVertexVec.front() == polyhedron[i].NeighborVertexVec.back()) polyhedron[i].NeighborVertexVec.pop_back();

							// {
							//   cerr << " : new NeighborVertexVec of " << i << " [";
							//   std::copy(polyhedron[i].NeighborVertexVec.begin(), polyhedron[i].NeighborVertexVec.end(), ostream_iterator<int>(cerr, " "));
							//   cerr << "]" << endl;
							// }

							// Make all the NeighborVertexVec of j point back at i instead of j.
							for (auto k : polyhedron[j].NeighborVertexVec)
							{
								// i is a neighbor to j, and j has already been removed from list
								// also, i can not be a neighbor to itself
								if (k != int(i))
								{
									auto itr = find(polyhedron[k].NeighborVertexVec.begin(), polyhedron[k].NeighborVertexVec.end(), j);
									// PCASSERT(itr != polyhedron[k].NeighborVertexVec.end());
									if (itr != polyhedron[k].NeighborVertexVec.end()) *itr = i;
								}
							}
						}
					}
				}
			}
		}

		// cerr << "After relinking: " << endl << polyhedron2string(polyhedron) << endl;

		if (active)
		{

			// Renumber the nodes assuming we're going to clear out the degenerates.
			auto offset = 0;
			for (auto i = 0u; i < n; ++i)
			{
				if (polyhedron[i].ID == -1)
				{
					--offset;
				}
				else
				{
					polyhedron[i].ID += offset;
				}
			}
			for (auto& v : polyhedron)
			{
				for (auto itr = v.NeighborVertexVec.begin(); itr < v.NeighborVertexVec.end(); ++itr)
				{
					*itr = polyhedron[*itr].ID;
				}
				v.NeighborVertexVec.erase(remove_if(v.NeighborVertexVec.begin(), v.NeighborVertexVec.end(), [](const int x) { return x < 0; }), v.NeighborVertexVec.end());

				// Remove any adjacent repeats.
				for (auto kitr = v.NeighborVertexVec.begin(); kitr < v.NeighborVertexVec.end() - 1; ++kitr)
				{
					if (*kitr == *(kitr + 1)) kitr = v.NeighborVertexVec.erase(kitr);
				}
				if (v.NeighborVertexVec.front() == v.NeighborVertexVec.back()) v.NeighborVertexVec.pop_back();
			}

			// Erase the inactive vertices.
			polyhedron.erase(remove_if(polyhedron.begin(), polyhedron.end(), [](const Vertex& v) { return v.ID < 0; }), polyhedron.end());
			if (polyhedron.size() < 4) polyhedron.clear();
		}
	}
}

int Poly::Compare(const Plane &plane, const double xmin, const double ymin, const double zmin, const double xmax,
                  const double ymax, const double zmax)
{
    const auto c1 = Compare(plane, Vector3(xmin, ymin, zmin));
    const auto c2 = Compare(plane, Vector3(xmax, ymin, zmin));
    const auto c3 = Compare(plane, Vector3(xmax, ymax, zmin));
    const auto c4 = Compare(plane, Vector3(xmin, ymax, zmin));
    const auto c5 = Compare(plane, Vector3(xmin, ymin, zmax));
    const auto c6 = Compare(plane, Vector3(xmax, ymin, zmax));
    const auto c7 = Compare(plane, Vector3(xmax, ymax, zmax));
    const auto c8 = Compare(plane, Vector3(xmin, ymax, zmax));
    const auto cmin = min(c1, min(c2, min(c3, min(c4, min(c5, min(c6, min(c7, c8)))))));
    const auto cmax = max(c1, max(c2, max(c3, max(c4, max(c5, max(c6, max(c7, c8)))))));

    if (cmin >= 0)
        return 1;
    else if (cmax <= 0)
        return -1;
    else
        return 0;
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
