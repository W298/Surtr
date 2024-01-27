#include "pch.h"
#include "VMACH.h"

VMACH::ConvexHull::ConvexHull(const std::vector<Point3D>& pointCloud, uint32_t limitCnt)
{
	m_pointCloud = pointCloud;
	m_limitCnt = limitCnt;

	m_pointVolume = std::vector<float>(m_pointCloud.size(), 0.0f);

	ConstructHull();
}

bool VMACH::ConvexHull::Contains(const Point3D& p) const
{
	for (const Face& f : m_faceList)
	{
		if (VolumeSign(f, p) <= 0)
			return false;
	}

	return true;
}

const std::list<VMACH::Face> VMACH::ConvexHull::GetFaces() const
{
	return m_faceList;
}

const std::vector<VMACH::Point3D> VMACH::ConvexHull::GetVertices() const
{
	return m_exteriorVertices;
}

bool VMACH::ConvexHull::Colinear(const Point3D& a, const Point3D& b, const Point3D& c)
{
	return
		((c.z - a.z) * (b.y - a.y) - (b.z - a.z) * (c.y - a.y)) == 0 &&
		((b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z)) == 0 &&
		((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) == 0;
}

float VMACH::ConvexHull::Volume(const Face& f, const Point3D& p)
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

int VMACH::ConvexHull::VolumeSign(const Face& f, const Point3D& p)
{
	float vol = Volume(f, p);
	if (vol == 0)
		return 0;

	return vol < 0 ? -1 : 1;
}

size_t VMACH::ConvexHull::Key2Edge(const Point3D& a, const Point3D& b)
{
	PointHash ph;
	return ph(a) ^ ph(b);
}

VMACH::Point3D VMACH::ConvexHull::FindInnerPoint(const Face* f, const Edge& e)
{
	for (int i = 0; i < 3; i++)
	{
		if (f->vertices[i] == e.endpoints[0])
			continue;
		if (f->vertices[i] == e.endpoints[1])
			continue;

		return f->vertices[i];
	}
}

void VMACH::ConvexHull::CreateFace(const Point3D& a, const Point3D& b, const Point3D& c, const Point3D& innerPoint)
{
	// Make sure face is CCW with face normal pointing outward
	m_faceList.emplace_back(a, b, c);
	Face& newFace = m_faceList.back();

	m_addedFaceVec.push_back(&newFace);

	if (VolumeSign(newFace, innerPoint) < 0)
		newFace.Reverse();

	CreateEdge(a, b, newFace);
	CreateEdge(a, c, newFace);
	CreateEdge(b, c, newFace);
}

void VMACH::ConvexHull::CreateEdge(const Point3D& p1, const Point3D& p2, Face& newFace)
{
	size_t key = Key2Edge(p1, p2);

	if (!m_edgeMap.count(key))
	{
		m_edgeList.emplace_back(p1, p2);
		m_edgeMap.insert({ key, &m_edgeList.back() });
	}

	m_edgeMap[key]->LinkAdjFace(&newFace);
}

void VMACH::ConvexHull::AddPointToHull(const Point3D& pt)
{
	// Find the illuminated faces (which will be removed later)
	bool atLeastOneVisible = false;
	for (Face& face : m_faceList)
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
		auto& face1 = edge.adjface1;
		auto& face2 = edge.adjface2;

		if (face1 == nullptr || face2 == nullptr)
			continue;
		else if (face1->visible && face2->visible)
			edge.remove = true;
		else if (face1->visible || face2->visible)
		{
			if (face1->visible)
				std::swap(face1, face2);

			Point3D innerPoint = FindInnerPoint(face2, edge);
			edge.Erase(face2);

			CreateFace(edge.endpoints[0], edge.endpoints[1], pt, innerPoint);
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
		[](const Point3D& a, const Point3D& b)
		{
			return a.x < b.x;
		});

	const auto v2 = std::max_element(std::begin(m_pointCloud), std::end(m_pointCloud),
		[&](const Point3D& a, const Point3D& b)
		{
			return std::sqrt(pow((a.x - (*v1).x), 2) + pow((a.y - (*v1).y), 2) + pow((a.z - (*v1).z), 2)) <
				std::sqrt(pow((b.x - (*v1).x), 2) + pow((b.y - (*v1).y), 2) + pow((b.z - (*v1).z), 2));
		});

	const auto v3 = std::max_element(std::begin(m_pointCloud), std::end(m_pointCloud),
		[&](const Point3D& a, const Point3D& b)
		{
			Face f1(*v1, *v2, a);
			Face f2(*v1, *v2, b);
			return f1.CalcArea() < f2.CalcArea();
		});

	const auto v4 = std::max_element(std::begin(m_pointCloud), std::end(m_pointCloud),
		[&](const Point3D& a, const Point3D& b)
		{
			Face f(*v1, *v2, *v3);
			return Volume(f, a) < Volume(f, b);
		});

	(*v1).processed = true;
	(*v2).processed = true;
	(*v3).processed = true;
	(*v4).processed = true;

	m_processedPointCnt += 4;

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

		for (const Face& f : m_faceList)
			m_pointVolume[i] += std::max(0.0f, Volume(f, m_pointCloud[i]));
	}

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
			size_t key = Key2Edge(it->endpoints[0], it->endpoints[1]);
			m_edgeMap.erase(key);
			m_edgeList.erase(it++);
		}
		else it++;
	};

	std::erase_if(m_faceList, [](const Face& f) { return f.visible; });
}

void VMACH::ConvexHull::ExtractExteriorPoints()
{
	std::unordered_set<Point3D, PointHash> exteriorSet;
	for (const Face& f : m_faceList)
		for (int i = 0; i < 3; i++)
			exteriorSet.insert(f.vertices[i]);

	m_exteriorVertices = std::vector<Point3D>(exteriorSet.begin(), exteriorSet.end());
}
