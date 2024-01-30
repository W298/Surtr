#include "pch.h"
#include "VMACH.h"

using namespace DirectX::SimpleMath;

bool VMACH::PolygonFace::IsEmpty()
{
	return vertexVec.size() == 0;
}

bool VMACH::PolygonFace::IsPointInsideFace(Vector3 point)
{
	double determinant = GetPointVsFaceDeterminant(point);
	return determinant <= 0;
}

int VMACH::PolygonFace::GetNumberOfEdges()
{
	return vertexVec.size();
}

double VMACH::PolygonFace::GetPointVsFaceDeterminant(Vector3 point)
{
	if (vertexVec.size() < 3)
		throw std::exception("Lack of vertex error.");

	Vector3 a = vertexVec[0];
	Vector3 b = vertexVec[1];
	Vector3 c = vertexVec[2];
	Vector3 x = point;

	Vector3 bDash = b - x;
	Vector3 cDash = c - x;
	Vector3 xDash = x - a;

	double determinant = bDash.x * (cDash.y * xDash.z - cDash.z * xDash.y) - bDash.y * (cDash.x * xDash.z - cDash.z * xDash.x) + bDash.z * (cDash.x * xDash.y - cDash.y * xDash.x);

	return determinant;
}

Vector3 VMACH::PolygonFace::GetIntersectionPoint(Vector3 p1, Vector3 p2)
{
	double determinantPoint1 = GetPointVsFaceDeterminant(p1);
	double determinantPoint2 = GetPointVsFaceDeterminant(p2);

	if (determinantPoint1 == determinantPoint2) 
	{
		Vector3 average;
		average = p1 + p2;
		average /= 2.0;

		return average;
	}
	else 
	{
		Vector3 intersect;
		intersect = p2 - p1;
		intersect *= (0 - determinantPoint1) / (determinantPoint2 - determinantPoint1);
		intersect += p1;

		return intersect;
	}
}

void VMACH::PolygonFace::AddVertex(Vector3 vertex)
{
	if (vertexVec.end() == std::find(vertexVec.begin(), vertexVec.end(), vertex))
		vertexVec.push_back(vertex);
}

void VMACH::PolygonFace::Rewind()
{
	std::reverse(vertexVec.begin(), vertexVec.end());
}

VMACH::PolygonFace VMACH::PolygonFace::ClipFace(PolygonFace clippingFace)
{
	PolygonFace	workingFace;

	for (int i = 0; i < GetNumberOfEdges(); i++)
	{
		Vector3 point1 = vertexVec[i];
		Vector3 point2 = vertexVec[(i + 1) % vertexVec.size()];

		if (clippingFace.IsPointInsideFace(point1) && clippingFace.IsPointInsideFace(point2))
		{
			workingFace.AddVertex(point2);
		}
		else if (clippingFace.IsPointInsideFace(point1) && FALSE == clippingFace.IsPointInsideFace(point2))
		{
			Vector3 intersection = clippingFace.GetIntersectionPoint(point1, point2);
			workingFace.AddVertex(intersection);
		}
		else if (FALSE == clippingFace.IsPointInsideFace(point1) && FALSE == clippingFace.IsPointInsideFace(point2))
		{
			continue;
		}
		else
		{
			Vector3 intersection = clippingFace.GetIntersectionPoint(point1, point2);
			
			workingFace.AddVertex(intersection);
			workingFace.AddVertex(point2);
		}
	}

	if (workingFace.GetNumberOfEdges() >= 3)
		return workingFace;
	else
		return PolygonFace();
}

VMACH::Polygon3D VMACH::Polygon3D::ClipPolygon(Polygon3D clippingPolygon)
{
	Polygon3D workingPolygon = *this;

	for (int i = 0; i < clippingPolygon.faceVec.size(); i++)
		workingPolygon = ClipFace(workingPolygon, clippingPolygon.faceVec[i]);

	return workingPolygon;
}

VMACH::Polygon3D VMACH::Polygon3D::ClipFace(Polygon3D inPolygon, PolygonFace clippingFace)
{
	// Clip Polygon3D
	Polygon3D outPolygon;
	for (int i = 0; i < inPolygon.faceVec.size(); i++)
	{
		PolygonFace clippedFace = inPolygon.faceVec[i].ClipFace(clippingFace);
		if (FALSE == clippedFace.IsEmpty())
			outPolygon.faceVec.push_back(clippedFace);
	}

	// Clip clippingFace because of section creation.
	PolygonFace workingFace = clippingFace;
	for (int i = 0; i < inPolygon.faceVec.size(); i++)
	{
		if (TRUE == clippingFace.IsEmpty())
			break;

		PolygonFace rewindedFace = inPolygon.faceVec[i];
		rewindedFace.Rewind();

		workingFace = workingFace.ClipFace(rewindedFace);
	}

	if (FALSE == workingFace.IsEmpty())
	{
		workingFace.Rewind();
		outPolygon.faceVec.push_back(workingFace);
	}

	return outPolygon;
}

VMACH::Polygon3D VMACH::Polygon3D::ClipFaces(Polygon3D inPolygon, std::vector<PolygonFace> clippingFaceVec)
{
	Polygon3D workingPolygon = *this;

	for (int i = 0; i < clippingFaceVec.size(); i++)
		workingPolygon = ClipFace(workingPolygon, clippingFaceVec[i]);

	return workingPolygon;
}

VMACH::ConvexHull::ConvexHull(const std::vector<ConvexHullVertex>& pointCloud, uint32_t limitCnt)
{
	m_pointCloud = pointCloud;
	m_limitCnt = limitCnt;

	m_pointVolume = std::vector<float>(m_pointCloud.size(), 0.0f);

	ConstructHull();
}

bool VMACH::ConvexHull::Contains(const ConvexHullVertex& p) const
{
	for (const ConvexHullFace& f : m_faceList)
	{
		if (VolumeSign(f, p) <= 0)
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

const std::vector<VMACH::ConvexHullVertex> VMACH::ConvexHull::GetVertices() const
{
	return m_exteriorVertices;
}

bool VMACH::ConvexHull::Colinear(const ConvexHullVertex& a, const ConvexHullVertex& b, const ConvexHullVertex& c)
{
	return
		((c.z - a.z) * (b.y - a.y) - (b.z - a.z) * (c.y - a.y)) == 0 &&
		((b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z)) == 0 &&
		((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) == 0;
}

float VMACH::ConvexHull::Volume(const ConvexHullFace& f, const ConvexHullVertex& p)
{
	float vol;
	float ax, ay, az, bx, by, bz, cx, cy, cz;

	ax = f.vertices[0].x - p.x;
	ay = f.vertices[0].y - p.y;
	az = f.vertices[0].z - p.z;
	bx = f.vertices[1].x - p.x;
	by = f.vertices[1].y - p.y;
	bz = f.vertices[1].z - p.z;
	cx = f.vertices[2].x - p.x;
	cy = f.vertices[2].y - p.y;
	cz = f.vertices[2].z - p.z;
	vol =
		ax * (by * cz - bz * cy) +
		ay * (bz * cx - bx * cz) +
		az * (bx * cy - by * cx);

	return vol;
}

int VMACH::ConvexHull::VolumeSign(const ConvexHullFace& f, const ConvexHullVertex& p)
{
	float vol = Volume(f, p);
	if (vol == 0)
		return 0;

	return vol < 0 ? -1 : 1;
}

size_t VMACH::ConvexHull::Key2Edge(const ConvexHullVertex& a, const ConvexHullVertex& b)
{
	PointHash ph;
	return ph(a) ^ ph(b);
}

VMACH::ConvexHullVertex VMACH::ConvexHull::FindInnerPoint(const ConvexHullFace* f, const ConvexHullEdge& e)
{
	for (int i = 0; i < 3; i++)
	{
		if (f->vertices[i] == e.endPoints[0])
			continue;
		if (f->vertices[i] == e.endPoints[1])
			continue;

		return f->vertices[i];
	}
}

void VMACH::ConvexHull::CreateFace(const ConvexHullVertex& a, const ConvexHullVertex& b, const ConvexHullVertex& c, const ConvexHullVertex& innerPoint)
{
	// Make sure face is CCW with face normal pointing outward
	m_faceList.emplace_back(a, b, c);
	ConvexHullFace& newFace = m_faceList.back();

	m_addedFaceVec.push_back(&newFace);

	if (VolumeSign(newFace, innerPoint) < 0)
		newFace.Rewind();

	CreateEdge(a, b, newFace);
	CreateEdge(a, c, newFace);
	CreateEdge(b, c, newFace);
}

void VMACH::ConvexHull::CreateEdge(const ConvexHullVertex& p1, const ConvexHullVertex& p2, ConvexHullFace& newFace)
{
	size_t key = Key2Edge(p1, p2);

	if (!m_edgeMap.count(key))
	{
		m_edgeList.emplace_back(p1, p2);
		m_edgeMap.insert({ key, &m_edgeList.back() });
	}

	m_edgeMap[key]->LinkFace(&newFace);
}

void VMACH::ConvexHull::AddPointToHull(const ConvexHullVertex& pt)
{
	// Find the illuminated faces (which will be removed later)
	bool atLeastOneVisible = false;
	for (ConvexHullFace& face : m_faceList)
	{
		if (VolumeSign(face, pt) < 0)
		{
			face.visible = atLeastOneVisible = true;
			m_visibleFaceVec.push_back(&face);
		}
	}

	if (FALSE == atLeastOneVisible)
		return;

	// Find the edges to make new tangent surface or to be removed
	for (auto it = m_edgeList.begin(); it != m_edgeList.end(); it++)
	{
		auto& edge = *it;
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

			CreateFace(edge.endPoints[0], edge.endPoints[1], pt, innerPoint);
		}
	}
}

bool VMACH::ConvexHull::BuildFirstHull()
{
	const int n = m_pointCloud.size();

	// Not enough points!
	if (n <= 3)
		return false;

	const auto v1 = std::max_element(std::begin(m_pointCloud), std::end(m_pointCloud),
		[](const ConvexHullVertex& a, const ConvexHullVertex& b)
		{
			return a.x < b.x;
		});

	const auto v2 = std::max_element(std::begin(m_pointCloud), std::end(m_pointCloud),
		[&](const ConvexHullVertex& a, const ConvexHullVertex& b)
		{
			return std::sqrt(pow((a.x - (*v1).x), 2) + pow((a.y - (*v1).y), 2) + pow((a.z - (*v1).z), 2)) <
				std::sqrt(pow((b.x - (*v1).x), 2) + pow((b.y - (*v1).y), 2) + pow((b.z - (*v1).z), 2));
		});

	const auto v3 = std::max_element(std::begin(m_pointCloud), std::end(m_pointCloud),
		[&](const ConvexHullVertex& a, const ConvexHullVertex& b)
		{
			ConvexHullFace f1(*v1, *v2, a);
			ConvexHullFace f2(*v1, *v2, b);
			return f1.CalcArea() < f2.CalcArea();
		});

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

	CreateFace(*v1, *v2, *v3, *v4);
	CreateFace(*v1, *v2, *v4, *v3);
	CreateFace(*v1, *v3, *v4, *v2);
	CreateFace(*v2, *v3, *v4, *v1);

	return true;
}

void VMACH::ConvexHull::ConstructHull()
{
	if (!BuildFirstHull())
		return;

	for (int i = 0; i < m_pointCloud.size(); i++)
	{
		if (m_pointCloud[i].processed)
			continue;

		for (const ConvexHullFace& f : m_faceList)
			m_pointVolume[i] += std::max(0.0f, Volume(f, m_pointCloud[i]));
	}

	if (m_limitCnt == 0)
		m_limitCnt = m_pointCloud.size();

	while (m_processedPointCnt < m_limitCnt)
	{
		int k = std::distance(m_pointVolume.begin(), std::max_element(m_pointVolume.begin(), m_pointVolume.end()));

		AddPointToHull(m_pointCloud[k]);
		m_pointCloud[k].processed = true;
		m_pointVolume[k] = -10000;

		m_processedPointCnt++;

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

	ExtractExteriorPoints();
}

void VMACH::ConvexHull::CleanUp()
{
	m_visibleFaceVec.clear();
	m_addedFaceVec.clear();

	auto it = m_edgeList.begin();
	while (it != m_edgeList.end())
	{
		if (it->remove)
		{
			size_t key = Key2Edge(it->endPoints[0], it->endPoints[1]);
			m_edgeMap.erase(key);
			m_edgeList.erase(it++);
		}
		else it++;
	};

	std::erase_if(m_faceList, [](const ConvexHullFace& f) { return f.visible; });
}

void VMACH::ConvexHull::ExtractExteriorPoints()
{
	std::unordered_set<ConvexHullVertex, PointHash> exteriorSet;
	for (const ConvexHullFace& f : m_faceList)
		for (int i = 0; i < 3; i++)
			exteriorSet.insert(f.vertices[i]);

	m_exteriorVertices = std::vector<ConvexHullVertex>(exteriorSet.begin(), exteriorSet.end());
}

VMACH::ConvexHullFace::ConvexHullFace(const ConvexHullVertex& p1, const ConvexHullVertex& p2, const ConvexHullVertex& p3) : visible(false)
{
	vertices[0] = p1; vertices[1] = p2; vertices[2] = p3;
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
	endPoints[0] = p1; endPoints[1] = p2;
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

std::size_t VMACH::PointHash::operator()(const ConvexHullVertex& p) const
{
	std::string sx, sy, sz;
	sx = std::to_string(p.x);
	sy = std::to_string(p.y);
	sz = std::to_string(p.z);
	return std::hash<std::string>{}(sx + sy + sz);
}
