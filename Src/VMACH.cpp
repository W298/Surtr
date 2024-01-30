#include "pch.h"
#include "VMACH.h"

using namespace DirectX::SimpleMath;

bool VMACH::PolygonFace::IsEmpty()
{
	return vertexVec.size() == 0;
}

double VMACH::PolygonFace::CalcDistanceToPoint(const Vector3& point) const
{
	return plane.Normal().Dot(point) + plane.w;
}

Vector3 VMACH::PolygonFace::GetIntersectionPoint(const Vector3& p1, const Vector3& p2) const
{
	return p1 + (p2 - p1) * (-CalcDistanceToPoint(p1) / plane.Normal().Dot(p2 - p1));
}

void VMACH::PolygonFace::AddVertex(Vector3 vertex)
{
	if (vertexVec.end() == std::find(vertexVec.begin(), vertexVec.end(), vertex))
		vertexVec.push_back(vertex);

	if (vertexVec.size() == 3)
		plane = Plane(vertexVec[0], vertexVec[1], vertexVec[2]);
}

void VMACH::PolygonFace::Rewind()
{
	std::reverse(vertexVec.begin(), vertexVec.end());
	plane = Plane(vertexVec[0], vertexVec[1], vertexVec[2]);
}

VMACH::PolygonFace VMACH::PolygonFace::ClipFace(const PolygonFace& inFace, const PolygonFace& clippingFace)
{
	PolygonFace	workingFace;

	for (int i = 0; i < inFace.vertexVec.size(); i++)
	{
		Vector3 point1 = inFace.vertexVec[i];
		Vector3 point2 = inFace.vertexVec[(i + 1) % inFace.vertexVec.size()];

		// (-) distance = inside polygon (plane).
		// (+) distance = outside polygon (plane).
		double d1 = clippingFace.CalcDistanceToPoint(point1);
		double d2 = clippingFace.CalcDistanceToPoint(point2);

		// IN, IN
		if (d1 <= 0 && d2 <= 0)
		{
			workingFace.AddVertex(point2);
		}
		// IN, OUT
		else if (d1 <= 0 && d2 > 0)
		{
			Vector3 intersection = clippingFace.GetIntersectionPoint(point1, point2);
			workingFace.AddVertex(intersection);
		}
		// OUT, OUT
		else if (d1 > 0 && d2 > 0)
		{
			continue;
		}
		// OUT, IN
		else
		{
			Vector3 intersection = clippingFace.GetIntersectionPoint(point1, point2);
			
			workingFace.AddVertex(intersection);
			workingFace.AddVertex(point2);
		}
	}

	return (workingFace.vertexVec.size() >= 3) ? workingFace : PolygonFace();
}

VMACH::Polygon3D VMACH::Polygon3D::ClipPolygon(const Polygon3D& inPolygon, const Polygon3D& clippingPolygon)
{
	Polygon3D outPolygon = inPolygon;

	// Clip polygon for each faces from clipping polygon.
	for (int i = 0; i < clippingPolygon.faceVec.size(); i++)
		outPolygon = ClipFace(outPolygon, clippingPolygon.faceVec[i]);

	return outPolygon;
}

VMACH::Polygon3D VMACH::Polygon3D::ClipFace(const Polygon3D& inPolygon, const PolygonFace& clippingFace)
{
	// Clip Polygon3D
	Polygon3D outPolygon;
	for (int i = 0; i < inPolygon.faceVec.size(); i++)
	{
		PolygonFace clippedFace = PolygonFace::ClipFace(inPolygon.faceVec[i], clippingFace);
		if (FALSE == clippedFace.IsEmpty())
			outPolygon.faceVec.push_back(clippedFace);
	}

	// Clip clippingFace because of section creation.
	PolygonFace workingFace = clippingFace;
	for (int i = 0; i < inPolygon.faceVec.size(); i++)
	{
		if (TRUE == workingFace.IsEmpty())
			break;

		workingFace = PolygonFace::ClipFace(workingFace, inPolygon.faceVec[i]);
	}

	if (FALSE == workingFace.IsEmpty())
		outPolygon.faceVec.push_back(workingFace);

	return outPolygon;
}

VMACH::ConvexHull::ConvexHull(const std::vector<ConvexHullVertex>& pointCloud, uint32_t limitCnt) 
	: m_pointCloud(pointCloud), m_limitCnt(limitCnt)
{
	m_pointVolume = std::vector<float>(m_pointCloud.size(), 0.0f);
	CreateConvexHull();
}

bool VMACH::ConvexHull::Contains(const ConvexHullVertex& point) const
{
	for (const ConvexHullFace& f : m_faceList)
	{
		if (Volume(f, point) <= 0)
			return false;
	}

	return true;
}

const std::list<VMACH::ConvexHullFace> VMACH::ConvexHull::GetFaces() const
{
	return m_faceList;
}

const std::list<VMACH::ConvexHullEdge> VMACH::ConvexHull::GetEdges() const
{
	return m_edgeList;
}

bool VMACH::ConvexHull::Colinear(const ConvexHullVertex& p1, const ConvexHullVertex& p2, const ConvexHullVertex& p3)
{
	return
		((p3.z - p1.z) * (p2.y - p1.y) - (p2.z - p1.z) * (p3.y - p1.y)) == 0 &&
		((p2.z - p1.z) * (p3.x - p1.x) - (p2.x - p1.x) * (p3.z - p1.z)) == 0 &&
		((p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x)) == 0;
}

float VMACH::ConvexHull::Volume(const ConvexHullFace& face, const ConvexHullVertex& point)
{
	float vol;
	float ax, ay, az, bx, by, bz, cx, cy, cz;

	ax = face.vertices[0].x - point.x;
	ay = face.vertices[0].y - point.y;
	az = face.vertices[0].z - point.z;
	bx = face.vertices[1].x - point.x;
	by = face.vertices[1].y - point.y;
	bz = face.vertices[1].z - point.z;
	cx = face.vertices[2].x - point.x;
	cy = face.vertices[2].y - point.y;
	cz = face.vertices[2].z - point.z;
	vol =
		ax * (by * cz - bz * cy) +
		ay * (bz * cx - bx * cz) +
		az * (bx * cy - by * cx);

	return vol;
}

size_t VMACH::ConvexHull::Key2Edge(const ConvexHullVertex& p1, const ConvexHullVertex& p2)
{
	PointHash ph;
	return ph(p1) ^ ph(p2);
}

VMACH::ConvexHullVertex VMACH::ConvexHull::FindInnerPoint(const ConvexHullFace* face, const ConvexHullEdge& edge)
{
	for (int i = 0; i < 3; i++)
	{
		if (face->vertices[i] == edge.endPoints[0])
			continue;
		if (face->vertices[i] == edge.endPoints[1])
			continue;

		return face->vertices[i];
	}
}

void VMACH::ConvexHull::CreateFace(const ConvexHullVertex& p1, const ConvexHullVertex& p2, const ConvexHullVertex& p3, const ConvexHullVertex& innerPoint)
{
	m_faceList.emplace_back(p1, p2, p3);
	ConvexHullFace& newFace = m_faceList.back();

	m_addedFaceVec.push_back(&newFace);

	// If needed, rewind face.
	if (Volume(newFace, innerPoint) < 0)
		newFace.Rewind();

	CreateEdge(p1, p2, newFace);
	CreateEdge(p1, p3, newFace);
	CreateEdge(p2, p3, newFace);
}

void VMACH::ConvexHull::CreateEdge(const ConvexHullVertex& p1, const ConvexHullVertex& p2, ConvexHullFace& newFace)
{
	// Get hash-key and apply to map.
	size_t key = Key2Edge(p1, p2);

	if (!m_edgeMap.count(key))
	{
		m_edgeList.emplace_back(p1, p2);
		m_edgeMap.insert({ key, &m_edgeList.back() });
	}

	m_edgeMap[key]->LinkFace(&newFace);
}

void VMACH::ConvexHull::AddPointToHull(const ConvexHullVertex& point)
{
	// Find the illuminated (will be removed) faces.
	bool atLeastOneVisible = false;
	for (ConvexHullFace& face : m_faceList)
	{
		if (Volume(face, point) < 0)
		{
			face.visible = true;
			m_visibleFaceVec.push_back(&face);

			atLeastOneVisible = true;
		}
	}

	if (FALSE == atLeastOneVisible)
		return;

	// Check edges. If needed, add faces.
	for (auto itr = m_edgeList.begin(); itr != m_edgeList.end(); itr++)
	{
		auto& edge = *itr;
		auto& face1 = edge.face1;
		auto& face2 = edge.face2;

		if (face1 == nullptr || face2 == nullptr)
			continue;
		else if (face1->visible && face2->visible)
			edge.remove = true;
		else if (face1->visible || face2->visible)
		{
			if (face1->visible)
				std::swap(face1, face2);

			ConvexHullVertex innerPoint = FindInnerPoint(face2, edge);
			edge.EraseFace(face2);

			CreateFace(edge.endPoints[0], edge.endPoints[1], point, innerPoint);
		}
	}
}

bool VMACH::ConvexHull::BuildFirstHull()
{
	// Not enough points!
	if (m_pointCloud.size() <= 3)
		return false;

	// Find x max point.
	const auto v1 = std::max_element(std::begin(m_pointCloud), std::end(m_pointCloud),
		[](const ConvexHullVertex& a, const ConvexHullVertex& b)
		{
			return a.x < b.x;
		});

	// Find length max point.
	const auto v2 = std::max_element(std::begin(m_pointCloud), std::end(m_pointCloud),
		[&](const ConvexHullVertex& a, const ConvexHullVertex& b)
		{
			return std::sqrt(pow((a.x - (*v1).x), 2) + pow((a.y - (*v1).y), 2) + pow((a.z - (*v1).z), 2)) <
				std::sqrt(pow((b.x - (*v1).x), 2) + pow((b.y - (*v1).y), 2) + pow((b.z - (*v1).z), 2));
		});

	// Find area max point.
	const auto v3 = std::max_element(std::begin(m_pointCloud), std::end(m_pointCloud),
		[&](const ConvexHullVertex& a, const ConvexHullVertex& b)
		{
			ConvexHullFace f1(*v1, *v2, a);
			ConvexHullFace f2(*v1, *v2, b);
			return f1.CalcArea() < f2.CalcArea();
		});

	// Find volume max point.
	const auto v4 = std::max_element(std::begin(m_pointCloud), std::end(m_pointCloud),
		[&](const ConvexHullVertex& a, const ConvexHullVertex& b)
		{
			ConvexHullFace f(*v1, *v2, *v3);
			return Volume(f, a) < Volume(f, b);
		});

	(*v1).processed = true;
	(*v2).processed = true;
	(*v3).processed = true;
	(*v4).processed = true;

	m_processedPointCnt = 4;

	// Create tetrahedron.
	CreateFace(*v1, *v2, *v3, *v4);
	CreateFace(*v1, *v2, *v4, *v3);
	CreateFace(*v1, *v3, *v4, *v2);
	CreateFace(*v2, *v3, *v4, *v1);

	return true;
}

void VMACH::ConvexHull::CreateConvexHull()
{
	if (!BuildFirstHull())
		return;

	// Init volume values.
	for (int i = 0; i < m_pointCloud.size(); i++)
	{
		if (m_pointCloud[i].processed)
			continue;

		for (const ConvexHullFace& f : m_faceList)
			m_pointVolume[i] += std::max(0.0f, Volume(f, m_pointCloud[i]));
	}

	if (m_limitCnt == 0)
		m_limitCnt = m_pointCloud.size();

	// Do until specified count is met.
	while (m_processedPointCnt < m_limitCnt)
	{
		// Greedy algorithm.
		// Find point index that maximizes volume.
		int k = std::distance(m_pointVolume.begin(), std::max_element(m_pointVolume.begin(), m_pointVolume.end()));

		AddPointToHull(m_pointCloud[k]);
		m_pointCloud[k].processed = true;
		m_pointVolume[k] = -10000;

		m_processedPointCnt++;

		// Apply changes to volume values.
		for (int i = 0; i < m_pointCloud.size(); i++)
		{
			if (m_pointCloud[i].processed)
				continue;

			float removedMaxVol = 0.0f;
			for (int j = 0; j < m_visibleFaceVec.size(); j++)
				removedMaxVol += std::max(0.0f, Volume(*m_visibleFaceVec[j], m_pointCloud[i]));

			float addedMaxVol = 0.0f;
			for (int j = 0; j < m_addedFaceVec.size(); j++)
				addedMaxVol += std::max(0.0f, Volume(*m_addedFaceVec[j], m_pointCloud[i]));

			m_pointVolume[i] -= removedMaxVol;
			m_pointVolume[i] += addedMaxVol;
		}

		CleanUp();
	}
}

void VMACH::ConvexHull::CleanUp()
{
	m_visibleFaceVec.clear();
	m_addedFaceVec.clear();

	// Erase flagged edges.
	auto itr = m_edgeList.begin();
	while (itr != m_edgeList.end())
	{
		if (itr->remove)
		{
			size_t key = Key2Edge(itr->endPoints[0], itr->endPoints[1]);
			m_edgeMap.erase(key);
			m_edgeList.erase(itr++);
		}
		else itr++;
	};

	// Erase flagged faces.
	std::erase_if(m_faceList, [](const ConvexHullFace& f) { return f.visible; });
}

VMACH::ConvexHullFace::ConvexHullFace(const ConvexHullVertex& p1, const ConvexHullVertex& p2, const ConvexHullVertex& p3) : visible(false)
{
	vertices[0] = p1; 
	vertices[1] = p2; 
	vertices[2] = p3;
}

void VMACH::ConvexHullFace::Rewind()
{
	std::swap(vertices[0], vertices[2]);
}

float VMACH::ConvexHullFace::CalcArea()
{
	ConvexHullVertex d1 = vertices[1] - vertices[0];
	ConvexHullVertex d2 = vertices[2] - vertices[0];

	return 0.5f * d1.Cross(d2).Length();
}

VMACH::ConvexHullEdge::ConvexHullEdge(const ConvexHullVertex& p1, const ConvexHullVertex& p2) : face1(nullptr), face2(nullptr), remove(false)
{
	endPoints[0] = p1;
	endPoints[1] = p2;
}

void VMACH::ConvexHullEdge::LinkFace(ConvexHullFace* face)
{
	if (face1 != nullptr && face2 != nullptr)
		return;

	(face1 == nullptr ? face1 : face2) = face;
}

void VMACH::ConvexHullEdge::EraseFace(ConvexHullFace* face)
{
	if (face1 != face && face2 != face)
		return;

	(face1 == face ? face1 : face2) = nullptr;
}

std::size_t VMACH::PointHash::operator()(const ConvexHullVertex& point) const
{
	std::string sx, sy, sz;
	sx = std::to_string(point.x);
	sy = std::to_string(point.y);
	sz = std::to_string(point.z);
	
	return std::hash<std::string>{}(sx + sy + sz);
}
