#include "pch.h"
#include "VMACH.h"

using namespace DirectX;
using namespace SimpleMath;

bool VMACH::PolygonFace::operator==(const PolygonFace& other)
{
	if (VertexVec.size() != other.VertexVec.size())
		return false;

	for (int i = 0; i < VertexVec.size(); i++)
	{
		if (other.VertexVec.end() == std::find_if(other.VertexVec.begin(), other.VertexVec.end(), [&](const Vector3& v) { return NearlyEqual(v, VertexVec[i]); }));
			return false;
	}

	return true;
}

bool VMACH::PolygonFace::IsEmpty() const
{
	return VertexVec.size() == 0;
}

bool VMACH::PolygonFace::IsCCW(const Vector3& normal) const
{
	Vector3 P = VertexVec[0];
	Vector3 S;
	
	for (int v = 0; v < VertexVec.size(); v++)
		S += 0.5f * (VertexVec[v] - P).Cross(VertexVec[(v + 1) % VertexVec.size()] - P);

	return S.Dot(normal) < 0;
}

bool VMACH::PolygonFace::IsConvex(const Vector3& normal) const
{
	for (int v = 0; v < VertexVec.size(); v++)
	{
		Vector3 a = VertexVec[(v - 1) < 0 ? VertexVec.size() - 1 : (v - 1)];
		Vector3 b = VertexVec[v];
		Vector3 c = VertexVec[(v + 1) % VertexVec.size()];

		if (GetAngleBetweenTwoVectorsOnPlane(b - a, c - a, normal) > XM_PI)
		{
			return false;
			break;
		}
	}

	return true;
}

double VMACH::PolygonFace::CalcDistanceToPoint(const Vector3& point) const
{
	Vector3 n = GetNormal();
	return n.Dot(point) + (double)FacePlane->w;
}

Vector3 VMACH::PolygonFace::GetIntersectionPoint(const Vector3& p1, const Vector3& p2) const
{
	Vector3 n = GetNormal();

	// Ensure calculation order.
	std::hash<std::string> sHash;
	size_t h1 = sHash(std::to_string(p1.x) + std::to_string(p1.y) + std::to_string(p1.z));
	size_t h2 = sHash(std::to_string(p2.x) + std::to_string(p2.y) + std::to_string(p2.z));

	const Vector3 v1 = (h1 < h2) ? p1 : p2;
	const Vector3 v2 = (h1 < h2) ? p2 : p1;

	return v1 + (v2 - v1) * (-CalcDistanceToPoint(v1) / n.Dot(v2 - v1));
}

Vector3 VMACH::PolygonFace::GetCentriod() const
{
	Vector3 centroid(0, 0, 0);
	for (int i = 0; i < VertexVec.size(); i++)
		centroid += VertexVec[i];
	centroid /= VertexVec.size();

	return centroid;
}

VMACH::Vector3 VMACH::PolygonFace::GetNormal() const
{
	if (FacePlane == nullptr)
		throw std::exception();

	Vector3 n = FacePlane->Normal();
	n.Normalize();

	return n;
}

void VMACH::PolygonFace::Render(std::vector<VertexNormalColor>& vertexData, std::vector<uint32_t>& indexData, const Vector3& color) const
{
	if (TRUE == GuaranteeConvex)
	{
		// If current polygon is always convex, do fan triangulization.
		Vector3 anchor = VertexVec[0];
		for (int v = 1; v < VertexVec.size() - 1; v++)
		{
			Vector3 a = VertexVec[v];
			Vector3 b = VertexVec[v + 1];

			vertexData.push_back(VertexNormalColor(anchor, XMFLOAT3(), color));
			vertexData.push_back(VertexNormalColor(a, XMFLOAT3(), color));
			vertexData.push_back(VertexNormalColor(b, XMFLOAT3(), color));

			indexData.push_back(indexData.size());
			indexData.push_back(indexData.size());
			indexData.push_back(indexData.size());
		}
	}
	else
	{
		// If current polygon can be non-convex, do ear clipping.
		Vector3 n = GetNormal();

		std::vector<int> vertexOrder(VertexVec.size());
		std::iota(vertexOrder.begin(), vertexOrder.end(), 0);

		std::unordered_set<int> E;

		const auto checkEar = [&]()
		{
			for (auto itr = vertexOrder.begin(); itr != vertexOrder.end(); itr++)
			{
				int prev = (itr == vertexOrder.begin()) ? vertexOrder.back() : *std::prev(itr);
				int cur = *itr;
				int next = (std::next(itr) == vertexOrder.end()) ? vertexOrder.front() : *std::next(itr);

				Vector3 a = VertexVec[prev];
				Vector3 b = VertexVec[cur];
				Vector3 c = VertexVec[next];

				// If triangle is not convex, ignore.
				if (GetAngleBetweenTwoVectorsOnPlane(b - a, c - a, n) > XM_PI)
					continue;

				bool pointInsideTri = false;

				for (auto iitr = vertexOrder.begin(); iitr != vertexOrder.end(); iitr++)
				{
					if (*iitr == prev || *iitr == cur || *iitr == next)
						continue;

					Vector3 x = VertexVec[*iitr];

					// Check any point is inside or not.
					if (GetAngleBetweenTwoVectorsOnPlane(b - a, x - a, n) > XM_PI)
						continue;
					if (GetAngleBetweenTwoVectorsOnPlane(c - b, x - b, n) > XM_PI)
						continue;
					if (GetAngleBetweenTwoVectorsOnPlane(a - c, x - c, n) > XM_PI)
						continue;

					pointInsideTri = true;
					break;
				}

				if (FALSE == pointInsideTri)
					E.insert(cur);
			}
		};

		checkEar();

		while (E.size() != 0)
		{
			if (vertexOrder.size() == 0)
				break;

			const auto itr = std::find(vertexOrder.begin(), vertexOrder.end(), *E.begin());
			if (itr == vertexOrder.end())
				throw std::exception();

			int prev = (itr == vertexOrder.begin()) ? vertexOrder.back() : *std::prev(itr);
			int cur = *itr;
			int next = (std::next(itr) == vertexOrder.end()) ? vertexOrder.front() : *std::next(itr);

			vertexData.push_back(VertexNormalColor(VertexVec[prev], XMFLOAT3(), color));
			vertexData.push_back(VertexNormalColor(VertexVec[cur], XMFLOAT3(), color));
			vertexData.push_back(VertexNormalColor(VertexVec[next], XMFLOAT3(), color));

			indexData.push_back(indexData.size());
			indexData.push_back(indexData.size());
			indexData.push_back(indexData.size());

			E.erase(E.begin());
			vertexOrder.erase(itr);

			checkEar();
		}
	}
}

void VMACH::PolygonFace::AddVertex(const Vector3& newVertex)
{
	// #CORRECTION
	if (VertexVec.end() != std::find_if(VertexVec.begin(), VertexVec.end(), [&](const Vector3& v) { return NearlyEqual(v, newVertex); }))
		return;

	VertexVec.push_back(newVertex);
	
	if (TRUE == GuaranteeConvex && VertexVec.size() == 3)
		ConstructFacePlane();
}

void VMACH::PolygonFace::ConstructFacePlane()
{
	// Construct face plane only if convex is guranteed and at least 3 vertices exists.
	if (TRUE == GuaranteeConvex && VertexVec.size() >= 3)
		FacePlane = std::make_unique<Plane>(VertexVec[0], VertexVec[1], VertexVec[2]);
}

void VMACH::PolygonFace::ManuallySetFacePlane(const Plane& plane)
{
	FacePlane = std::make_unique<Plane>(plane);
}

void VMACH::PolygonFace::Rewind()
{
	std::reverse(VertexVec.begin(), VertexVec.end());
	ConstructFacePlane();
}

void VMACH::PolygonFace::__Reorder()
{
	// Depreciated. Need re-implementation.
	Vector3 centroid = GetCentriod();
	centroid += Vector3(EPSILON, EPSILON, EPSILON);

	Vector3 n = GetNormal();

	std::sort(VertexVec.begin() + 1, VertexVec.end(), [&](const Vector3& a, const Vector3& b) 
		{ 
			return 
				GetAngleBetweenTwoVectorsOnPlane(VertexVec[0] - centroid, a - centroid, n) >
				GetAngleBetweenTwoVectorsOnPlane(VertexVec[0] - centroid, b - centroid, n);
		});
}

VMACH::PolygonFace VMACH::PolygonFace::ClipFace(const PolygonFace& inFace, const PolygonFace& clippingFace, std::vector<PolygonEdge>& edgeVec)
{
	PolygonFace	workingFace = { inFace.GuaranteeConvex };
	
	// If face is non-convex, face plane is not constructed automatically.
	if (FALSE == workingFace.GuaranteeConvex)
		workingFace.ManuallySetFacePlane(*clippingFace.FacePlane);

	PolygonEdge clippedEdge;

	for (int i = 0; i < inFace.VertexVec.size(); i++)
	{
		Vector3 point1 = inFace.VertexVec[i];
		Vector3 point2 = inFace.VertexVec[(i + 1) % inFace.VertexVec.size()];

		// (-) distance = inside polygon (plane).
		// (+) distance = outside polygon (plane).
		double d1 = clippingFace.CalcDistanceToPoint(point1);
		double d2 = clippingFace.CalcDistanceToPoint(point2);

		// #CORRECTION
		// IN, IN
		if (d1 <= +EPSILON && d2 <= +EPSILON)
		{
			workingFace.AddVertex(point2);
		}
		// IN, OUT
		else if (d1 <= +EPSILON && d2 > -EPSILON)
		{
			Vector3 intersection = clippingFace.GetIntersectionPoint(point1, point2);
			clippedEdge.VertexVec.push_back(intersection);

			workingFace.AddVertex(intersection);
		}
		// OUT, OUT
		else if (d1 > -EPSILON && d2 > -EPSILON)
		{
			continue;
		}
		// OUT, IN
		else
		{
			Vector3 intersection = clippingFace.GetIntersectionPoint(point1, point2);
			clippedEdge.VertexVec.push_back(intersection);

			workingFace.AddVertex(intersection);
			workingFace.AddVertex(point2);
		}
	}

	if (clippedEdge.VertexVec.size() == 2)
		edgeVec.push_back(clippedEdge);

	return (workingFace.VertexVec.size() >= 3) ? workingFace : PolygonFace(inFace.GuaranteeConvex);
}

Vector3 VMACH::Polygon3D::GetCentroid() const
{
	Vector3 centroid(0, 0, 0);
	for (int i = 0; i < FaceVec.size(); i++)
		centroid += FaceVec[i].GetCentriod();
	centroid /= FaceVec.size();

	return centroid;
}

bool VMACH::Polygon3D::Contains(const Vector3& point) const
{
	for (const PolygonFace& f : FaceVec)
	{
		if (f.CalcDistanceToPoint(point) > +EPSILON)
			return false;
	}

	return true;
}

void VMACH::Polygon3D::Render(std::vector<VertexNormalColor>& vertexData, std::vector<uint32_t>& indexData, const Vector3& color) const
{
	for (int f = 0; f < FaceVec.size(); f++)
		FaceVec[f].Render(vertexData, indexData, color);
}

void VMACH::Polygon3D::AddFace(const PolygonFace& newFace)
{
	if (TRUE == GuaranteeConvex && FALSE == newFace.GuaranteeConvex)
		GuaranteeConvex = FALSE;
	
	FaceVec.push_back(newFace);
}

void VMACH::Polygon3D::Translate(const Vector3& vector)
{
	for (int f = 0; f < FaceVec.size(); f++)
		for (int v = 0; v < FaceVec[f].VertexVec.size(); v++)
			FaceVec[f].VertexVec[v] += vector;
}

void VMACH::Polygon3D::Scale(const float& scalar)
{
	for (int f = 0; f < FaceVec.size(); f++)
		for (int v = 0; v < FaceVec[f].VertexVec.size(); v++)
			FaceVec[f].VertexVec[v] *= scalar;
}

void VMACH::Polygon3D::Scale(const Vector3& vector)
{
	for (int f = 0; f < FaceVec.size(); f++)
		for (int v = 0; v < FaceVec[f].VertexVec.size(); v++)
			FaceVec[f].VertexVec[v] *= vector;
}

VMACH::Polygon3D VMACH::Polygon3D::ClipFace(const Polygon3D& inPolygon, const PolygonFace& clippingFace)
{
	std::vector<PolygonEdge> edgeVec;

	// Clip Polygon3D
	Polygon3D outPolygon = { inPolygon.GuaranteeConvex };
	for (int i = 0; i < inPolygon.FaceVec.size(); i++)
	{
		PolygonFace clippedFace = PolygonFace::ClipFace(inPolygon.FaceVec[i], clippingFace, edgeVec);
		if (FALSE == clippedFace.IsEmpty())
			outPolygon.AddFace(clippedFace);
	}

	if (edgeVec.size() < 3)
		return outPolygon;

	Vector3 cn = clippingFace.GetNormal();

	const auto extractFace = [&]() 
	{
		// Closing face can be non-convex when inPolygon is not guranteed.
		PolygonFace closeFace = { inPolygon.GuaranteeConvex };

		closeFace.AddVertex(edgeVec[0].VertexVec[0]);
		closeFace.AddVertex(edgeVec[0].VertexVec[1]);

		Vector3 findPoint = edgeVec[0].VertexVec[1];

		std::swap(edgeVec[0], edgeVec.back());
		edgeVec.pop_back();

		bool polygonClosed = false;
		while (TRUE)
		{
			bool err = true;
			for (int e = 0; e < edgeVec.size(); e++)
			{
				int id = -1;
				for (int v = 0; v < 2; v++)
				{
					if (TRUE == NearlyEqual(edgeVec[e].VertexVec[v], findPoint))
					{
						id = v;
						break;
					}
				}

				if (id != -1)
				{
					err = false;
					Vector3 connectedVertex = edgeVec[e].VertexVec[(id + 1) % 2];

					// Polygon closed.
					if (NearlyEqual(connectedVertex, closeFace.VertexVec.front()))
					{
						std::swap(edgeVec[e], edgeVec.back());
						edgeVec.pop_back();

						polygonClosed = true;
						break;
					}

					closeFace.AddVertex(connectedVertex);
					findPoint = connectedVertex;

					std::swap(edgeVec[e], edgeVec.back());
					edgeVec.pop_back();

					break;
				}
			}

			// #TEST ignore error faces.
			if (TRUE == err)
				return;

			if (TRUE == polygonClosed)
				break;
		}

		
		// Check closing face is CCW or CW.
		if (closeFace.IsCCW(cn))
			closeFace.Rewind();

		// Set face plane if convex is not guranteed.
		// Check closing face is really non-convex or not.
		if (FALSE == closeFace.GuaranteeConvex)
		{
			closeFace.ManuallySetFacePlane(*clippingFace.FacePlane);
			closeFace.GuaranteeConvex = closeFace.IsConvex(cn);
		}

		outPolygon.AddFace(closeFace);
	};

	// #TEST
	while (edgeVec.size() != 0)
		extractFace();

	return outPolygon;
}

VMACH::Polygon3D VMACH::Polygon3D::ClipPolygon(const Polygon3D& inPolygon, const Polygon3D& clippingPolygon)
{
	Polygon3D outPolygon = inPolygon;

	// Clip polygon for each faces from clipping polygon.
	for (int i = 0; i < clippingPolygon.FaceVec.size(); i++)
		outPolygon = ClipFace(outPolygon, clippingPolygon.FaceVec[i]);

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

void VMACH::ConvexHull::Render(std::vector<VertexNormalColor>& vertexData, std::vector<uint32_t>& indexData, Vector3 color) const
{
	for (const VMACH::ConvexHullFace& f : m_faceList)
	{
		vertexData.push_back(VertexNormalColor(f.Vertices[0], XMFLOAT3(), color));
		vertexData.push_back(VertexNormalColor(f.Vertices[1], XMFLOAT3(), color));
		vertexData.push_back(VertexNormalColor(f.Vertices[2], XMFLOAT3(), color));

		indexData.push_back(indexData.size());
		indexData.push_back(indexData.size());
		indexData.push_back(indexData.size());
	}
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

	ax = face.Vertices[0].x - point.x;
	ay = face.Vertices[0].y - point.y;
	az = face.Vertices[0].z - point.z;
	bx = face.Vertices[1].x - point.x;
	by = face.Vertices[1].y - point.y;
	bz = face.Vertices[1].z - point.z;
	cx = face.Vertices[2].x - point.x;
	cy = face.Vertices[2].y - point.y;
	cz = face.Vertices[2].z - point.z;
	vol =
		ax * (by * cz - bz * cy) +
		ay * (bz * cx - bx * cz) +
		az * (bx * cy - by * cx);

	return vol;
}

size_t VMACH::ConvexHull::Key2Edge(const ConvexHullVertex& p1, const ConvexHullVertex& p2)
{
	std::hash<std::string> sHash;
	size_t h1 = sHash(std::to_string(p1.x) + std::to_string(p1.y) + std::to_string(p1.z));
	size_t h2 = sHash(std::to_string(p2.x) + std::to_string(p2.y) + std::to_string(p2.z));

	return h1 ^ h2;
}

VMACH::ConvexHullVertex VMACH::ConvexHull::FindInnerPoint(const ConvexHullFace* face, const ConvexHullEdge& edge)
{
	for (int i = 0; i < 3; i++)
	{
		if (face->Vertices[i] == edge.EndPoints[0])
			continue;
		if (face->Vertices[i] == edge.EndPoints[1])
			continue;

		return face->Vertices[i];
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
			face.Visible = true;
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
		auto& face1 = edge.Face1;
		auto& face2 = edge.Face2;

		if (face1 == nullptr || face2 == nullptr)
			continue;
		else if (face1->Visible && face2->Visible)
			edge.Remove = true;
		else if (face1->Visible || face2->Visible)
		{
			if (face1->Visible)
				std::swap(face1, face2);

			ConvexHullVertex innerPoint = FindInnerPoint(face2, edge);
			edge.EraseFace(face2);

			CreateFace(edge.EndPoints[0], edge.EndPoints[1], point, innerPoint);
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

	(*v1).Processed = true;
	(*v2).Processed = true;
	(*v3).Processed = true;
	(*v4).Processed = true;

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
		if (m_pointCloud[i].Processed)
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
		m_pointCloud[k].Processed = true;
		m_pointVolume[k] = -FLT_MAX;

		m_processedPointCnt++;

		// Apply changes to volume values.
		for (int i = 0; i < m_pointCloud.size(); i++)
		{
			if (m_pointCloud[i].Processed)
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
		if (itr->Remove)
		{
			size_t key = Key2Edge(itr->EndPoints[0], itr->EndPoints[1]);
			m_edgeMap.erase(key);
			m_edgeList.erase(itr++);
		}
		else itr++;
	};

	// Erase flagged faces.
	std::erase_if(m_faceList, [](const ConvexHullFace& f) { return f.Visible; });
}

VMACH::ConvexHullFace::ConvexHullFace(const ConvexHullVertex& p1, const ConvexHullVertex& p2, const ConvexHullVertex& p3) : Visible(false)
{
	Vertices[0] = p1; 
	Vertices[1] = p2; 
	Vertices[2] = p3;
}

void VMACH::ConvexHullFace::Rewind()
{
	std::swap(Vertices[0], Vertices[2]);
}

float VMACH::ConvexHullFace::CalcArea()
{
	ConvexHullVertex d1 = Vertices[1] - Vertices[0];
	ConvexHullVertex d2 = Vertices[2] - Vertices[0];

	return 0.5f * d1.Cross(d2).Length();
}

VMACH::ConvexHullEdge::ConvexHullEdge(const ConvexHullVertex& p1, const ConvexHullVertex& p2) : Face1(nullptr), Face2(nullptr), Remove(false)
{
	EndPoints[0] = p1;
	EndPoints[1] = p2;
}

void VMACH::ConvexHullEdge::LinkFace(ConvexHullFace* face)
{
	if (Face1 != nullptr && Face2 != nullptr)
		return;

	(Face1 == nullptr ? Face1 : Face2) = face;
}

void VMACH::ConvexHullEdge::EraseFace(ConvexHullFace* face)
{
	if (Face1 != face && Face2 != face)
		return;

	(Face1 == face ? Face1 : Face2) = nullptr;
}

bool VMACH::NearlyEqual(const Vector3& v1, const Vector3& v2)
{
	return (v1 - v2).Length() < EPSILON;
}

VMACH::Polygon3D VMACH::GetBoxPolygon()
{
	Polygon3D boxPolygon(true, 
	{
		PolygonFace(true, 
		{
			Vector3(+0.5f, -0.5f, -0.5f), Vector3(+0.5f, +0.5f, -0.5f),
			Vector3(-0.5f, +0.5f, -0.5f), Vector3(-0.5f, -0.5f, -0.5f)
		}),
		PolygonFace(true,
		{
			Vector3(+0.5f, -0.5f, +0.5f), Vector3(+0.5f, +0.5f, +0.5f),
			Vector3(+0.5f, +0.5f, -0.5f), Vector3(+0.5f, -0.5f, -0.5f)
		}),
		PolygonFace(true,
		{
			Vector3(-0.5f, -0.5f, +0.5f), Vector3(-0.5f, +0.5f, +0.5f),
			Vector3(+0.5f, +0.5f, +0.5f), Vector3(+0.5f, -0.5f, +0.5f)
		}),
		PolygonFace(true,
		{
			Vector3(-0.5f, -0.5f, -0.5f), Vector3(-0.5f, +0.5f, -0.5f),
			Vector3(-0.5f, +0.5f, +0.5f), Vector3(-0.5f, -0.5f, +0.5f)
		}),
		PolygonFace(true,
		{
			Vector3(+0.5f, +0.5f, +0.5f), Vector3(-0.5f, +0.5f, +0.5f),
			Vector3(-0.5f, +0.5f, -0.5f), Vector3(+0.5f, +0.5f, -0.5f)
		}),
		PolygonFace(true,
		{
			Vector3(-0.5f, -0.5f, +0.5f), Vector3(+0.5f, -0.5f, +0.5f),
			Vector3(+0.5f, -0.5f, -0.5f), Vector3(-0.5f, -0.5f, -0.5f)
		})
	});

	for (int f = 0; f < boxPolygon.FaceVec.size(); f++)
		boxPolygon.FaceVec[f].Rewind();

	return boxPolygon;
}

float VMACH::GetAngleBetweenTwoVectorsOnPlane(const Vector3& v1, const Vector3& v2, const Vector3& n)
{
	float dot = v1.Dot(v2);
	float det =
		v1.x * v2.y * n.z +
		v2.x * n.y * v1.z +
		n.x * v1.y * v2.z -
		v1.z * v2.y * n.x -
		v2.z * n.y * v1.x -
		n.z * v1.y * v2.x;

	float angle = atan2(det, dot);
	angle = (angle < 0) ? XM_2PI + angle : angle;

	return angle;
}
