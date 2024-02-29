#include "pch.h"
#include "Surtr.h"

#include "voro++.hh"

#define PVD_HOST "127.0.0.1"
#define MAX_NUM_ACTOR_SHAPES 512
#define MAX_NUM_ACTOR_HIT 512

extern void ExitGame() noexcept;

using namespace DirectX;
using namespace SimpleMath;
using Microsoft::WRL::ComPtr;
using namespace physx;

const auto rnd = []() { return double(rand() * 0.75) / RAND_MAX; };

static PxDefaultAllocator			gAllocator;
static PxDefaultErrorCallback		gErrorCallback;
static PxFoundation*				gFoundation			= NULL;
static PxPhysics*					gPhysics			= NULL;
static PxDefaultCpuDispatcher*		gDispatcher			= NULL;
static PxScene*						gScene				= NULL;
static PxMaterial*					gMaterial			= NULL;
static PxPvd*						gPvd				= NULL;

dp::thread_pool g_threadPool(16);

Surtr::Surtr() noexcept :
	m_window(nullptr),
	m_outputWidth(1280),
	m_outputHeight(720),
	m_backBufferIndex(0),
	m_rtvDescriptorSize(0),
	m_dsvDescriptorSize(0),
	m_cbvSrvDescriptorSize(0),
	m_featureLevel(D3D_FEATURE_LEVEL_11_0),
	m_fenceValues {}
{
}

Surtr::~Surtr()
{
	// Ensure that the GPU is no longer referencing resources that are about to be destroyed.
	WaitForGpu();

	// Reset fullscreen state on destroy.
	if (m_fullScreenMode)
		DX::ThrowIfFailed(m_swapChain->SetFullscreenState(FALSE, NULL));
}

static XMMATRIX PxMatToXMMATRIX(PxMat44 mat)
{
	return XMMatrixSet(mat.column0.x, mat.column0.y, mat.column0.z, mat.column0.w,
					   mat.column1.x, mat.column1.y, mat.column1.z, mat.column1.w,
					   mat.column2.x, mat.column2.y, mat.column2.z, mat.column2.w,
					   mat.column3.x, mat.column3.y, mat.column3.z, mat.column3.w);
}

int work(int t, int id)
{
	printf("%d start \n", id);
	std::this_thread::sleep_for(std::chrono::seconds(t));
	return t + id;
}

// Initialize the Direct3D resources required to run.
void Surtr::InitializeD3DResources(HWND window, int width, int height, UINT modelIndex, UINT shadowMapSize, BOOL fullScreenMode)
{
	m_window = window;
	m_outputWidth = std::max(width, 1);
	m_outputHeight = std::max(height, 1);
	m_aspectRatio = static_cast<float>(m_outputWidth) / static_cast<float>(m_outputHeight);
	m_fullScreenMode = fullScreenMode;
	m_modelIndex = modelIndex;

	// Initialize values.
	m_executeFractureImmediate = false;
	m_isFlightMode = true;

	m_shadowMapSize = shadowMapSize;

	m_renderShadow = true;
	m_lightRotation = false;

	m_sceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_sceneBounds.Radius = 100.0f;

	m_camUp = DEFAULT_UP_VECTOR;
	m_camForward = DEFAULT_FORWARD_VECTOR;
	m_camRight = DEFAULT_RIGHT_VECTOR;
	m_camYaw = -XM_PI;
	m_camPitch = 0.0f;
	m_camPosition = XMVectorSet(3.0f, 7.0f, 18.0f, 0.0f);
	m_camLookTarget = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
	m_camMoveSpeed = 5.0f;
	m_camRotateSpeed = 0.5f;

	m_viewMatrix = XMMatrixLookAtLH(m_camPosition, m_camLookTarget, DEFAULT_UP_VECTOR);

	m_lightDirection = XMVectorSet(1.0f, -0.2f, 0.0f, 1.0f);
	m_lightDirection = XMVector3TransformCoord(m_lightDirection, XMMatrixRotationY(3.0f));

	m_shadowTransform = IDENTITY_MATRIX;
	m_lightNearZ = 0.0f;
	m_lightFarZ = 0.0f;
	m_lightPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_lightView = IDENTITY_MATRIX;
	m_lightProj = IDENTITY_MATRIX;

	CreateDeviceResources();
	CreateDeviceDependentResources();
	CreateWindowSizeDependentResources();
	CreateCommandListDependentResources();

	// Fixed timestep for simulation.
	m_timer.SetFixedTimeStep(true);
	m_timer.SetTargetElapsedSeconds(1.0 / 120.0);
}

// Executes the basic game loop.
void Surtr::Tick()
{
	m_timer.Tick([&]()
	{
		Update(m_timer);
	});

	Render();
}

void Surtr::OnKeyDown(UINT8 key)
{
	if (key == VK_ESCAPE) // Exit game.
	{
		ExitGame();
		return;
	}

	if (key == 'X') // Change mouse mode.
	{
		m_isFlightMode = !m_isFlightMode;
		ShowCursor(!m_isFlightMode);
		return;
	}

	m_keyTracker.insert_or_assign(key, true);
}

void Surtr::OnKeyUp(UINT8 key)
{
	m_keyTracker.insert_or_assign(key, false);
}

void Surtr::OnMouseWheel(float delta)
{
	// Scroll to adjust move speed.
	m_camMoveSpeed = std::max(m_camMoveSpeed + delta * 0.005f, 0.0f);
}

void Surtr::OnMouseMove(int x, int y)
{
	// If it's not flight mode, return.
	if (!m_isFlightMode)
		return;

	// Flight mode camera rotation.
	m_camYaw += x * 0.001f * m_camRotateSpeed;
	m_camPitch = std::min(std::max(m_camPitch + y * 0.001f * m_camRotateSpeed, -XM_PIDIV2), XM_PIDIV2);

	// Reset cursor position.
	POINT pt = { m_outputWidth / 2, m_outputHeight / 2 };
	ClientToScreen(m_window, &pt);
	SetCursorPos(pt.x, pt.y);
}

void Surtr::OnMouseDown()
{
	if (m_isFlightMode)
		return;

	POINT pt;
	GetCursorPos(&pt);
	ScreenToClient(m_window, &pt);

	double x = ((double)pt.x / m_outputWidth) * 2 - 1;
	double y = 1 - ((double)pt.y / m_outputHeight) * 2;

	Vector3 clipSpaceCoord = Vector3(x, y, 0);
	Vector3 viewSpaceCoord = XMVector3TransformCoord(clipSpaceCoord, XMMatrixInverse(nullptr, m_projectionMatrix));
	Vector3 worldSpaceCoord = XMVector3TransformCoord(viewSpaceCoord, XMMatrixInverse(nullptr, m_viewMatrix));

	Vector3 rayDir = worldSpaceCoord - Vector3(m_camPosition);
	rayDir.Normalize();

	const Vector3 camPosV3 = Vector3(m_camPosition);

	PxVec3 origin(camPosV3.x, camPosV3.y, camPosV3.z);
	PxVec3 direction(rayDir.x, rayDir.y, rayDir.z);
	PxReal maxDistance = 1000;

	for (const auto& compoundMesh : m_fractureStorage.CompoundMeshVec)
		for (DynamicMesh* mesh : compoundMesh)
			mesh->DebugValue = 0;
	
	PxRaycastBuffer hit;
	if (TRUE == gScene->raycast(origin, direction, maxDistance, hit))
	{
		Vector3 hitPos = Vector3(hit.block.position.x, hit.block.position.y, hit.block.position.z);
		m_fractureArgs.ImpactPosition = hitPos + rayDir * m_fractureArgs.TargetAdder;

		if (TRUE == m_fractureArgs.RadialMode)
		{
			PxOverlapHit overlapBuffer[MAX_NUM_ACTOR_HIT];
			PxOverlapBuffer buf(overlapBuffer, MAX_NUM_ACTOR_HIT);

			PxSphereGeometry overlapSphere(m_fractureArgs.ImpactRadius / 2.0);
			PxTransform shapePose = PxTransform(PxVec3(m_fractureArgs.ImpactPosition.x, m_fractureArgs.ImpactPosition.y, m_fractureArgs.ImpactPosition.z));

			if (TRUE == gScene->overlap(overlapSphere, shapePose, buf, PxQueryFilterData(PxQueryFlag::eDYNAMIC)))
			{
				m_affectRigidBodyVec.clear();
				for (int i = 0; i < buf.nbTouches; i++)
				{
					PxRigidActor* target = buf.touches[i].actor;
					
					if (1e-4 < ((PxRigidDynamic*)target)->getMass())
						m_affectRigidBodyVec.push_back(target);
					else
						SetRigidBodyDebugValue(target, 2);
				}
			}
		}
		else
		{
			m_affectRigidBodyVec.clear();
			m_affectRigidBodyVec.push_back(hit.block.actor);
		}
	}

	for (PxRigidActor* rigidBody : m_affectRigidBodyVec)
		SetRigidBodyDebugValue(rigidBody, 1);

	std::vector<VertexNormalColor> vertices(m_sphereVertexData.size());
	std::transform(m_sphereVertexData.begin(), m_sphereVertexData.end(), vertices.begin(),
				   [&](const VertexNormalColor& vnc)
				   { return VertexNormalColor(vnc.Position * m_fractureArgs.ImpactRadius + m_fractureArgs.ImpactPosition, XMFLOAT3(), XMFLOAT3(0, 0, 1)); });
	UpdateDynamicMesh(m_impactPointMesh, vertices, m_impactPointMesh->IndexData);

	if (TRUE == m_executeFractureImmediate)
		for (PxRigidActor* rigidBody : m_affectRigidBodyVec)
			ExecuteFractureRoutine(rigidBody);
}

// Updates the world.
void Surtr::Update(DX::StepTimer const& timer)
{
	const auto elapsedTime = static_cast<float>(timer.GetElapsedSeconds());

	// Set view matrix based on camera position and orientation.
	m_camRotationMatrix = XMMatrixRotationRollPitchYaw(m_camPitch, m_camYaw, 0.0f);
	m_camLookTarget = XMVector3TransformCoord(DEFAULT_FORWARD_VECTOR, m_camRotationMatrix);
	m_camLookTarget = XMVector3Normalize(m_camLookTarget);

	m_camRight = XMVector3TransformCoord(DEFAULT_RIGHT_VECTOR, m_camRotationMatrix);
	m_camUp = XMVector3TransformCoord(DEFAULT_UP_VECTOR, m_camRotationMatrix);
	m_camForward = XMVector3TransformCoord(DEFAULT_FORWARD_VECTOR, m_camRotationMatrix);

	// Flight mode.
	const float verticalMove = (m_keyTracker['W'] ? 1.0f : m_keyTracker['S'] ? -1.0f : 0.0f) * elapsedTime * m_camMoveSpeed;
	const float horizontalMove = (m_keyTracker['A'] ? -1.0f : m_keyTracker['D'] ? 1.0f : 0.0f) * elapsedTime * m_camMoveSpeed;
	const float upDownMove = (m_keyTracker['Q'] ? -1.0f : m_keyTracker['E'] ? 1.0f : 0.0f) * elapsedTime * m_camMoveSpeed;

	m_camPosition += horizontalMove * m_camRight;
	m_camPosition += verticalMove * m_camForward;
	m_camPosition += upDownMove * m_camUp;

	m_camLookTarget = m_camPosition + m_camLookTarget;
	m_viewMatrix = XMMatrixLookAtLH(m_camPosition, m_camLookTarget, m_camUp);

	// Update projection matrix.
	m_projectionMatrix = XMMatrixPerspectiveFovLH(XM_PIDIV4, m_aspectRatio, 0.01f, 1000);

	// Light rotation update.
	if (m_lightRotation)
		m_lightDirection = XMVector3TransformCoord(m_lightDirection, XMMatrixRotationY(elapsedTime / 24.0f));

	// Update Shadow Transform.
	{
		XMVECTOR lightDir = m_lightDirection;
		XMVECTOR lightPos = -2.0f * m_sceneBounds.Radius * lightDir;
		XMVECTOR targetPos = XMLoadFloat3(&m_sceneBounds.Center);
		XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

		XMStoreFloat3(&m_lightPosition, lightPos);

		// Transform bounding sphere to light space.
		XMFLOAT3 sphereCenterLS;
		XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

		// Ortho frustum in light space encloses scene.
		float l = sphereCenterLS.x - m_sceneBounds.Radius;
		float b = sphereCenterLS.y - m_sceneBounds.Radius;
		float n = sphereCenterLS.z - m_sceneBounds.Radius;
		float r = sphereCenterLS.x + m_sceneBounds.Radius;
		float t = sphereCenterLS.y + m_sceneBounds.Radius;
		float f = sphereCenterLS.z + m_sceneBounds.Radius;

		m_lightNearZ = n;
		m_lightFarZ = f;

		XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

		// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
		XMMATRIX T(
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f);

		XMMATRIX S = lightView * lightProj * T;
		XMStoreFloat4x4(&m_lightView, lightView);
		XMStoreFloat4x4(&m_lightProj, lightProj);
		XMStoreFloat4x4(&m_shadowTransform, S);
	}

	// Step physx simulation.
	gScene->simulate(1.0f/120.0f);
	gScene->fetchResults(true);

	// Update world matrix.
	PxShape* shapes[MAX_NUM_ACTOR_SHAPES];

	int id = 0;
	for (int i = 0; i < m_fractureStorage.RigidDynamicVec.size(); i++)
	{
		PxRigidDynamic* rigidBody = m_fractureStorage.RigidDynamicVec[i];
		if (rigidBody == nullptr)
			continue;

		rigidBody->getShapes(shapes, 1);
		if (shapes[0] == nullptr)
			continue;

		const PxMat44 shapePose(PxShapeExt::getGlobalPose(*shapes[0], *rigidBody));
		const XMMATRIX mat = PxMatToXMMATRIX(shapePose);

		for (int j = 0; j < m_fractureStorage.CompoundMeshVec[i].size(); j++)
		{
			m_structuredBufferData[id].WorldMatrix = XMMatrixTranspose(mat);

			id++;
		}
	}
}

void Surtr::UploadStructuredBuffer()
{
	CD3DX12_RANGE readRange(0, 0);
	UINT8* bufferBegin = nullptr;

	DX::ThrowIfFailed(m_sbUploadHeap->Map(0, &readRange, reinterpret_cast<void**>(&bufferBegin)));
	memcpy(bufferBegin, m_structuredBufferData.data(), sizeof(MeshSB) * m_structuredBufferData.size());
	m_sbUploadHeap->Unmap(0, nullptr);
}

// Draws the scene.
void Surtr::Render()
{
	// Don't try to render anything before the first Update.
	if (m_timer.GetFrameCount() == 0)
	{
		return;
	}

	WaitForGpu();

	// ----------> Prepare command list.
	DX::ThrowIfFailed(m_commandAllocators[m_backBufferIndex]->Reset());
	DX::ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_backBufferIndex].Get(), nullptr));

	UploadStructuredBuffer();

	// Set root signature & descriptor table.
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

	m_commandList->SetDescriptorHeaps(1, m_srvDescriptorHeapSB.GetAddressOf());
	m_commandList->SetGraphicsRootDescriptorTable(4, m_srvDescriptorHeapSB->GetGPUDescriptorHandleForHeapStart());

	m_commandList->SetDescriptorHeaps(1, m_srvDescriptorHeap.GetAddressOf());
	m_commandList->SetGraphicsRootDescriptorTable(0, m_srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	// PASS 1 - Shadow Map
	if (m_renderShadow)
	{
		// Update ShadowCB Data
		{
			ShadowCB cbShadow;

			const XMMATRIX lightWorld = XMLoadFloat4x4(&IDENTITY_MATRIX);
			const XMMATRIX lightView = XMLoadFloat4x4(&m_lightView);
			const XMMATRIX lightProj = XMLoadFloat4x4(&m_lightProj);

			cbShadow.LightWorldMatrix = XMMatrixTranspose(lightWorld);
			cbShadow.LightViewProjMatrix = XMMatrixTranspose(lightView * lightProj);
			cbShadow.CameraPosition = m_camPosition;

			memcpy(&m_cbShadowMappedData[m_backBufferIndex], &cbShadow, sizeof(ShadowCB));

			// Bind the constants to the shader.
			const auto baseGpuAddress = m_cbShadowGpuAddress + m_backBufferIndex * sizeof(ShadowCB);
			m_commandList->SetGraphicsRootConstantBufferView(2, baseGpuAddress);
		}

		// Set render target as nullptr.
		const auto dsv = m_shadowMap->Dsv();
		m_commandList->OMSetRenderTargets(0, nullptr, false, &dsv);

		// Set PSO.
		m_commandList->SetPipelineState(m_shadowPSO.Get());

		// Set the viewport and scissor rect.
		const auto viewport = m_shadowMap->Viewport();
		const auto scissorRect = m_shadowMap->ScissorRect();
		m_commandList->RSSetViewports(1, &viewport);
		m_commandList->RSSetScissorRects(1, &scissorRect);

		// Translate depth/stencil buffer to DEPTH_WRITE.
		const D3D12_RESOURCE_BARRIER toWrite = CD3DX12_RESOURCE_BARRIER::Transition(
			m_shadowMap->Resource(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		m_commandList->ResourceBarrier(1, &toWrite);

		// ---> DEPTH_WRITE
		{
			// Clear DSV.
			m_commandList->ClearDepthStencilView(
				m_shadowMap->Dsv(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

			// Set Topology and VB.
			m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			int id = 0;
			for (const auto& compoundMeshes : m_fractureStorage.CompoundMeshVec)
			{
				for (MeshBase* mesh : compoundMeshes)
				{
					if (StaticMesh::RenderOptionType::NOT_RENDER ^ mesh->RenderOption)
						mesh->Render(m_commandList.Get(), id);

					id++;
				}
			}
		}
		// <--- GENERIC_READ

		// Translate depth/stencil buffer to GENERIC_READ.
		const D3D12_RESOURCE_BARRIER toRead = CD3DX12_RESOURCE_BARRIER::Transition(
			m_shadowMap->Resource(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ);
		m_commandList->ResourceBarrier(1, &toRead);
	}

	// PASS 2 - Opaque.
	{
		// Update OpaqueCB data.
		{
			OpaqueCB cbOpaque;

			cbOpaque.ViewProjMatrix = XMMatrixTranspose(m_viewMatrix * m_projectionMatrix);
			XMStoreFloat4(&cbOpaque.CameraPosition, m_camPosition);
			XMStoreFloat4(&cbOpaque.LightDirection, m_lightDirection);
			cbOpaque.LightColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

			cbOpaque.ShadowTransform = XMMatrixTranspose(XMLoadFloat4x4(&m_shadowTransform));

			memcpy(&m_cbOpaqueMappedData[m_backBufferIndex], &cbOpaque, sizeof(OpaqueCB));

			// Bind OpaqueCB data to the shader.
			const auto baseGPUAddress = m_cbOpaqueGpuAddress + m_backBufferIndex * sizeof(OpaqueCB);
			m_commandList->SetGraphicsRootConstantBufferView(1, baseGPUAddress);
		}

		// Get handle of RTV, DSV.
		const CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
			m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			static_cast<INT>(m_backBufferIndex), m_rtvDescriptorSize);
		const CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

		// Set render target.
		m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

		// Set PSO.
		m_commandList->SetPipelineState(m_renderShadow ? m_opaquePSO.Get() : m_noShadowPSO.Get());

		// Set the viewport and scissor rect.
		m_commandList->RSSetViewports(1, &m_viewport);
		m_commandList->RSSetScissorRects(1, &m_scissorRect);

		// Translate render target to WRITE state.
		const D3D12_RESOURCE_BARRIER toWrite = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_backBufferIndex].Get(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_commandList->ResourceBarrier(1, &toWrite);

		// ---> D3D12_RESOURCE_STATE_RENDER_TARGET
		{
			// Clear RTV, DSV.
			m_commandList->ClearRenderTargetView(rtvHandle, GRAY, 0, nullptr);
			m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			// Set Topology and VB.
			m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			int id = 0;
			for (const auto& compoundMeshes : m_fractureStorage.CompoundMeshVec)
			{
				for (MeshBase* mesh : compoundMeshes)
				{
					if (StaticMesh::RenderOptionType::SOLID & mesh->RenderOption)
						mesh->Render(m_commandList.Get(), id);
					
					id++;
				}
			}

			m_groundMesh->Render(m_commandList.Get(), 99999);

			m_commandList->SetPipelineState(m_wireframePSO.Get());

			id = 0;
			for (const auto& compoundMeshes : m_fractureStorage.CompoundMeshVec)
			{
				for (MeshBase* mesh : compoundMeshes)
				{
					if ((StaticMesh::RenderOptionType::WIREFRAME & mesh->RenderOption) && (StaticMesh::RenderOptionType::SOLID & mesh->RenderOption))
						mesh->Render(m_commandList.Get(), id);

					id++;
				}
			}

			m_commandList->SetPipelineState(m_coloredWireframePSO.Get());

			id = 0;
			for (const auto& compoundMeshes : m_fractureStorage.CompoundMeshVec)
			{
				for (MeshBase* mesh : compoundMeshes)
				{
					if ((StaticMesh::RenderOptionType::WIREFRAME & mesh->RenderOption) && !(StaticMesh::RenderOptionType::SOLID & mesh->RenderOption))
						mesh->Render(m_commandList.Get(), id);

					id++;
				}
			}

			m_patternBoundaryMesh->Render(m_commandList.Get(), 99999);
			m_impactPointMesh->Render(m_commandList.Get(), 99999);

			// Draw imgui.
			{
				ImGui_ImplDX12_NewFrame();
				ImGui_ImplWin32_NewFrame();
				ImGui::NewFrame();

				{
					const auto io = ImGui::GetIO();
					ImGui::Begin("Surtr", NULL, ImGuiWindowFlags_AlwaysAutoResize);

					ImGui::Text("%d x %d (Resolution)", m_outputWidth, m_outputHeight);
					ImGui::Text("%d x %d (Shadow Map Resolution)", m_shadowMapSize, m_shadowMapSize);
					ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

					ImGui::Dummy(ImVec2(0.0f, 20.0f));

					ImGui::Text("[Arguments]");

					ImGui::Checkbox("Execute Immediate", &m_executeFractureImmediate);
					ImGui::Checkbox("Radial Mode", &m_fractureArgs.RadialMode);
					ImGui::Checkbox("Partial Fracture", &m_fractureArgs.PartialFracture);
					ImGui::SliderFloat("Impact Radius", &m_fractureArgs.ImpactRadius, 0.1f, 10.0f);
					ImGui::Text("Impact Point: %.3f %.3f %.3f", m_fractureArgs.ImpactPosition.x, m_fractureArgs.ImpactPosition.y, m_fractureArgs.ImpactPosition.z);

					ImGui::Dummy(ImVec2(0.0f, 10.0f));

					ImGui::SliderInt("Seed", &m_fractureArgs.Seed, 0, 100000);

					ImGui::Dummy(ImVec2(0.0f, 10.0f));

					if (ImGui::Button("Simulate!"))
					{
						for (PxRigidActor* rigidBody : m_affectRigidBodyVec)
							ExecuteFractureRoutine(rigidBody);
					}

					ImGui::Text("[Results]");
					ImGui::Text("ICH Face Count: %d", m_fractureResult.ICHFaceCnt);

					if (m_fractureResult.ACHErrorPointCnt == 0)
						ImGui::TextColored(ImVec4(0, 1, 0, 1), "ALL VERTEX CONTAINED");
					else
						ImGui::TextColored(ImVec4(1, 0, 0, 1), "%d VERTEX NOT CONTAINED!", m_fractureResult.ACHErrorPointCnt);

					ImGui::Dummy(ImVec2(0.0f, 20.0f));

					ImGui::SliderFloat("Rotate speed", &m_camRotateSpeed, 0.0f, 1.0f);
					ImGui::Text("Move speed: %.3f (Scroll to Adjust)", m_camMoveSpeed);

					ImGui::Dummy(ImVec2(0.0f, 20.0f));

					ImGui::Checkbox("Rotate Light", &m_lightRotation);
					ImGui::Checkbox("Render Shadow", &m_renderShadow);

					ImGui::Dummy(ImVec2(0.0f, 20.0f));

					if (ImGui::Button("Reset Camera"))
					{
						m_camYaw = 0.0f;
						m_camPitch = 0.0f;
						m_camPosition = XMVectorSet(0.0f, 0.0f, -500.0f, 0.0f);
						m_camLookTarget = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
					}

					ImGui::Dummy(ImVec2(0.0f, 20.0f));

					ImGui::Text("Press X to Switch mouse mode");
					ImGui::Text("(GUI Mode <-> Flight Mode)");

					ImGui::End();
				}

				ImGui::Render();
				ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());
			}
		}
		// <--- D3D12_RESOURCE_STATE_PRESENT

		// Translate render target to READ state.
		const D3D12_RESOURCE_BARRIER toRead = CD3DX12_RESOURCE_BARRIER::Transition(
			m_renderTargets[m_backBufferIndex].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		m_commandList->ResourceBarrier(1, &toRead);
	}

	// <---------- Close and execute command list.
	DX::ThrowIfFailed(m_commandList->Close());
	m_commandQueue->ExecuteCommandLists(1, CommandListCast(m_commandList.GetAddressOf()));

	// Present back buffer.
	const HRESULT hr = m_swapChain->Present(0, m_fullScreenMode ? 0 : DXGI_PRESENT_ALLOW_TEARING);

	// If the device was reset we must completely reinitialize the renderer.
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
		OnDeviceLost();
	}
	else
	{
		DX::ThrowIfFailed(hr);
		MoveToNextFrame();
	}
}

// Message handlers
void Surtr::OnActivated()
{
	// TODO: Game is becoming active window.
}

void Surtr::OnDeactivated()
{
	// TODO: Game is becoming background window.
}

void Surtr::OnSuspending()
{
	// TODO: Game is being power-suspended (or minimized).
}

void Surtr::OnResuming()
{
	m_timer.ResetElapsedTime();

	// TODO: Game is being power-resumed (or returning from minimize).
}

void Surtr::OnWindowSizeChanged(int width, int height)
{
	if (!m_window)
		return;

	m_outputWidth = std::max(width, 1);
	m_outputHeight = std::max(height, 1);
	m_aspectRatio = static_cast<float>(m_outputWidth) / static_cast<float>(m_outputHeight);

	CreateWindowSizeDependentResources();
}

// These are the resources that depend on the device.
void Surtr::CreateDeviceResources()
{
	// Enable the debug layer.
#if defined(_DEBUG)
	ComPtr<ID3D12Debug> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf()))))
	{
		debugController->EnableDebugLayer();
	}
#endif

	// ================================================================================================================
	// #01. Create DXGI Device.
	// ================================================================================================================
	{
		// Create the DXGI factory.
		DWORD dxgiFactoryFlags = 0;
		DX::ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf())));

		// Get adapter.
		ComPtr<IDXGIAdapter1> adapter;
		GetAdapter(adapter.GetAddressOf());

		// Create the DX12 API device object.
		DX::ThrowIfFailed(
			D3D12CreateDevice(
				adapter.Get(),
				m_featureLevel,
				IID_PPV_ARGS(m_d3dDevice.ReleaseAndGetAddressOf())
			));

		// Check Shader Model 6 support.
		D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_0 };
		if (FAILED(m_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)))
			|| (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_0))
		{
#ifdef _DEBUG
			OutputDebugStringA("ERROR: Shader Model 6.0 is not supported!\n");
#endif
			throw std::runtime_error("Shader Model 6.0 is not supported!");
		}

#ifndef NDEBUG
		// Configure debug device (if active).
		ComPtr<ID3D12InfoQueue> d3dInfoQueue;
		if (SUCCEEDED(m_d3dDevice.As(&d3dInfoQueue)))
		{
#ifdef _DEBUG
			d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
#endif
			D3D12_MESSAGE_ID hide[] =
			{
				D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
				D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
				// Workarounds for debug layer issues on hybrid-graphics systems
				D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_WRONGSWAPCHAINBUFFERREFERENCE,
				D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
			};
			D3D12_INFO_QUEUE_FILTER filter = {};
			filter.DenyList.NumIDs = static_cast<UINT>(std::size(hide));
			filter.DenyList.pIDList = hide;
			d3dInfoQueue->AddStorageFilterEntries(&filter);
		}
#endif

		// Get descriptor sizes.
		m_rtvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_dsvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		m_cbvSrvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	// ================================================================================================================
	// #02. Create fence objects.
	// ================================================================================================================
	{
		DX::ThrowIfFailed(
			m_d3dDevice->CreateFence(
				m_fenceValues[m_backBufferIndex],
				D3D12_FENCE_FLAG_NONE,
				IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf())));

		m_fenceValues[m_backBufferIndex]++;

		m_fenceEvent.Attach(CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE));
		if (!m_fenceEvent.IsValid())
		{
			throw std::system_error(
				std::error_code(static_cast<int>(GetLastError()), std::system_category()), "CreateEventEx");
		}
	}

	// ================================================================================================================
	// #03. Create command objects.
	// ================================================================================================================
	{
		// Create command queue.
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommandQueue(
				&queueDesc,
				IID_PPV_ARGS(m_commandQueue.ReleaseAndGetAddressOf())));

		// Create a command allocator for each back buffer that will be rendered to.
		for (UINT n = 0; n < c_swapBufferCount; n++)
		{
			DX::ThrowIfFailed(
				m_d3dDevice->CreateCommandAllocator(
					D3D12_COMMAND_LIST_TYPE_DIRECT,
					IID_PPV_ARGS(m_commandAllocators[n].ReleaseAndGetAddressOf())));
		}

		// Create a command list for recording graphics commands.
		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommandList(
				0,
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				m_commandAllocators[0].Get(),
				nullptr,
				IID_PPV_ARGS(m_commandList.ReleaseAndGetAddressOf())));

		DX::ThrowIfFailed(m_commandList->Close());
	}
}

void Surtr::CreateDeviceDependentResources()
{
	// ================================================================================================================
	// #01. Create descriptor heaps.
	// ================================================================================================================
	{
		// Create RTV descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {};
		rtvDescriptorHeapDesc.NumDescriptors = c_swapBufferCount;
		rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

		DX::ThrowIfFailed(
			m_d3dDevice->CreateDescriptorHeap(
				&rtvDescriptorHeapDesc,
				IID_PPV_ARGS(m_rtvDescriptorHeap.ReleaseAndGetAddressOf())));

		// Create DSV descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc = {};
		dsvDescriptorHeapDesc.NumDescriptors = 2;   // one for shadow pass, one for opaque pass.
		dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

		DX::ThrowIfFailed(
			m_d3dDevice->CreateDescriptorHeap(
				&dsvDescriptorHeapDesc,
				IID_PPV_ARGS(m_dsvDescriptorHeap.ReleaseAndGetAddressOf())));

		// Create SRV descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC srvDescriptorHeapDesc = {};
		srvDescriptorHeapDesc.NumDescriptors = 6;   // color map (2), displacement map (2), shadow map (1), imgui (1).
		srvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		DX::ThrowIfFailed(
			m_d3dDevice->CreateDescriptorHeap(
				&srvDescriptorHeapDesc,
				IID_PPV_ARGS(m_srvDescriptorHeap.ReleaseAndGetAddressOf())));

		// Create SRV descriptor heap (for structured bufer).
		D3D12_DESCRIPTOR_HEAP_DESC srvDescriptorHeapSBDesc = {};
		srvDescriptorHeapSBDesc.NumDescriptors = 1;
		srvDescriptorHeapSBDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvDescriptorHeapSBDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

		DX::ThrowIfFailed(
			m_d3dDevice->CreateDescriptorHeap(
				&srvDescriptorHeapSBDesc,
				IID_PPV_ARGS(m_srvDescriptorHeapSB.ReleaseAndGetAddressOf())));
	}

	// ================================================================================================================
	// #02. Create root signature.
	// ================================================================================================================
	{
		// Define root parameters.
		CD3DX12_DESCRIPTOR_RANGE srvTable;
		srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0);		// register (t0)

		CD3DX12_DESCRIPTOR_RANGE srvTableSB;
		srvTableSB.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7);		// register (t7)

		CD3DX12_ROOT_PARAMETER rootParameters[5] = {};
		rootParameters[0].InitAsDescriptorTable(1, &srvTable);  
		rootParameters[1].InitAsConstantBufferView(0);				// register (b0)
		rootParameters[2].InitAsConstantBufferView(1);				// register (b1)
		rootParameters[3].InitAsConstants(2u, 4u);					// register (b4, space0)
		rootParameters[4].InitAsDescriptorTable(1, &srvTableSB);

		// Define samplers.
		const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
			0,                                                  // register (s0)
			D3D12_FILTER_ANISOTROPIC,                           // filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,                   // addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,                   // addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,                   // addressW
			0.0f,                                               // mipLODBias
			16,                                                 // maxAnisotropy
			D3D12_COMPARISON_FUNC_LESS_EQUAL,
			D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
			0.0f,                                               // minLOD
			D3D12_FLOAT32_MAX,                                  // maxLOD
			D3D12_SHADER_VISIBILITY_ALL
		);

		const CD3DX12_STATIC_SAMPLER_DESC shadow(
			1,                                                  // register (s1)
			D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,   // filter
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,                  // addressU
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,                  // addressV
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,                  // addressW
			0.0f,                                               // mipLODBias
			16,                                                 // maxAnisotropy
			D3D12_COMPARISON_FUNC_LESS,
			D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK
		);

		const CD3DX12_STATIC_SAMPLER_DESC anisotropicClampMip1(
			2,                                                  // register (s2)
			D3D12_FILTER_ANISOTROPIC,                           // filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,                   // addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,                   // addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,                   // addressW
			0.0f,                                               // mipLODBias
			16,                                                 // maxAnisotropy
			D3D12_COMPARISON_FUNC_LESS_EQUAL,
			D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
			1,                                                  // minLOD
			D3D12_FLOAT32_MAX                                   // maxLOD
		);

		CD3DX12_STATIC_SAMPLER_DESC staticSamplers[3] =
		{
			anisotropicClamp, shadow, anisotropicClampMip1
		};

		// Create root signature.
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(
			_countof(rootParameters), rootParameters,
			_countof(staticSamplers), staticSamplers,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);

		ComPtr<ID3DBlob> signatureBlob;
		ComPtr<ID3DBlob> errorBlob;

		DX::ThrowIfFailed(
			D3D12SerializeRootSignature(
				&rootSignatureDesc,
				D3D_ROOT_SIGNATURE_VERSION_1,
				&signatureBlob,
				&errorBlob));

		DX::ThrowIfFailed(
			m_d3dDevice->CreateRootSignature(
				0,
				signatureBlob->GetBufferPointer(),
				signatureBlob->GetBufferSize(),
				IID_PPV_ARGS(m_rootSignature.ReleaseAndGetAddressOf())));
	}

	// ================================================================================================================
	// #03. Create PSO.
	// ================================================================================================================
	{
		static constexpr D3D12_INPUT_ELEMENT_DESC c_inputElementDesc[] =
		{
			{ "POSITION",   0,  DXGI_FORMAT_R32G32B32_FLOAT,    0,  0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL",     0,  DXGI_FORMAT_R32G32B32_FLOAT,    0,  12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR",      0,  DXGI_FORMAT_R32G32B32_FLOAT,    0,  24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		// Load compiled shaders.
		auto vertexShaderBlob = DX::ReadData(L"VS.cso");
		auto pixelShaderBlob = DX::ReadData(L"PS.cso");

		// Create Opaque PSO.
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { c_inputElementDesc, _countof(c_inputElementDesc) };
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS = { vertexShaderBlob.data(), vertexShaderBlob.size() };
		psoDesc.PS = { pixelShaderBlob.data(), pixelShaderBlob.size() };
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.DSVFormat = c_depthBufferFormat;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = c_rtvFormat;
		psoDesc.SampleDesc.Count = 1;
		DX::ThrowIfFailed(
			m_d3dDevice->CreateGraphicsPipelineState(
				&psoDesc,
				IID_PPV_ARGS(m_opaquePSO.ReleaseAndGetAddressOf())));

		// Create Wireframe PSO.
		auto wireframePSBlob = DX::ReadData(L"WireframePS.cso");
		auto wireframePSODesc = D3D12_GRAPHICS_PIPELINE_STATE_DESC(psoDesc);
		wireframePSODesc.PS = { wireframePSBlob.data(), wireframePSBlob.size() };
		wireframePSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		wireframePSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		DX::ThrowIfFailed(
			m_d3dDevice->CreateGraphicsPipelineState(
				&wireframePSODesc,
				IID_PPV_ARGS(m_wireframePSO.ReleaseAndGetAddressOf())));

		// Create Colored Wireframe PSO.
		auto coloredWireframePSBlob = DX::ReadData(L"ColoredWireframePS.cso");
		auto coloredWireframePSODesc = D3D12_GRAPHICS_PIPELINE_STATE_DESC(psoDesc);
		coloredWireframePSODesc.PS = { coloredWireframePSBlob.data(), coloredWireframePSBlob.size() };
		coloredWireframePSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		coloredWireframePSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		DX::ThrowIfFailed(
			m_d3dDevice->CreateGraphicsPipelineState(
				&coloredWireframePSODesc,
				IID_PPV_ARGS(m_coloredWireframePSO.ReleaseAndGetAddressOf())));

		// Create No shadow PSO.
		auto noShadowPSBlob = DX::ReadData(L"NoShadowPS.cso");
		auto noShadowPSODesc = D3D12_GRAPHICS_PIPELINE_STATE_DESC(psoDesc);
		noShadowPSODesc.PS = { noShadowPSBlob.data(), noShadowPSBlob.size() };
		DX::ThrowIfFailed(
			m_d3dDevice->CreateGraphicsPipelineState(
				&noShadowPSODesc,
				IID_PPV_ARGS(m_noShadowPSO.ReleaseAndGetAddressOf())));

		// Load compiled shadow shaders.
		auto shadowVSBlob = DX::ReadData(L"ShadowVS.cso");
		auto shadowPSBlob = DX::ReadData(L"ShadowPS.cso");

		// Create Shadow Map PSO.
		auto shadowPSODesc = D3D12_GRAPHICS_PIPELINE_STATE_DESC(psoDesc);
		shadowPSODesc.VS = { shadowVSBlob.data(), shadowVSBlob.size() };
		shadowPSODesc.PS = { shadowPSBlob.data(), shadowPSBlob.size() };
		shadowPSODesc.RasterizerState.DepthBias = 100000;
		shadowPSODesc.RasterizerState.DepthBiasClamp = 0.0f;
		shadowPSODesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
		shadowPSODesc.pRootSignature = m_rootSignature.Get();
		shadowPSODesc.DSVFormat = c_depthBufferFormat;
		shadowPSODesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
		shadowPSODesc.NumRenderTargets = 0;
		DX::ThrowIfFailed(
			m_d3dDevice->CreateGraphicsPipelineState(
				&shadowPSODesc,
				IID_PPV_ARGS(m_shadowPSO.ReleaseAndGetAddressOf())));
	}

	// ================================================================================================================
	// #04. Create constant buffer and map.
	// ================================================================================================================
	{
		// Create opaque constant buffer.
		{
			CD3DX12_HEAP_PROPERTIES uploadHeapProp(D3D12_HEAP_TYPE_UPLOAD);
			CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(c_swapBufferCount * sizeof(OpaqueCB));
			DX::ThrowIfFailed(
				m_d3dDevice->CreateCommittedResource(
					&uploadHeapProp,
					D3D12_HEAP_FLAG_NONE,
					&resDesc,
					D3D12_RESOURCE_STATE_GENERIC_READ,
					nullptr,
					IID_PPV_ARGS(m_cbOpaqueUploadHeap.ReleaseAndGetAddressOf())));

			// Mapping.
			DX::ThrowIfFailed(m_cbOpaqueUploadHeap->Map(0, nullptr, reinterpret_cast<void**>(&m_cbOpaqueMappedData)));
			m_cbOpaqueGpuAddress = m_cbOpaqueUploadHeap->GetGPUVirtualAddress();
		}

		// Create shadow constant buffer.
		{
			CD3DX12_HEAP_PROPERTIES uploadHeapProp(D3D12_HEAP_TYPE_UPLOAD);
			CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(c_swapBufferCount * sizeof(OpaqueCB));
			DX::ThrowIfFailed(
				m_d3dDevice->CreateCommittedResource(
					&uploadHeapProp,
					D3D12_HEAP_FLAG_NONE,
					&resDesc,
					D3D12_RESOURCE_STATE_GENERIC_READ,
					nullptr,
					IID_PPV_ARGS(m_cbShadowUploadHeap.ReleaseAndGetAddressOf())));

			// Mapping.
			DX::ThrowIfFailed(m_cbShadowUploadHeap->Map(0, nullptr, reinterpret_cast<void**>(&m_cbShadowMappedData)));
			m_cbShadowGpuAddress = m_cbShadowUploadHeap->GetGPUVirtualAddress();
		}
	}

	// ================================================================================================================
	// #05. Build shadow resources.
	// ================================================================================================================
	{
		m_shadowMap = std::make_unique<ShadowMap>(m_d3dDevice.Get(), m_shadowMapSize, m_shadowMapSize);
		m_shadowMap->BuildDescriptors(
			CD3DX12_CPU_DESCRIPTOR_HANDLE(m_srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), 4, m_cbvSrvDescriptorSize),
			CD3DX12_GPU_DESCRIPTOR_HANDLE(m_srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 4, m_cbvSrvDescriptorSize),
			CD3DX12_CPU_DESCRIPTOR_HANDLE(m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), 1, m_dsvDescriptorSize));
	}

	// ================================================================================================================
	// #06. Setup imgui context.
	// ================================================================================================================
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		auto io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

		// Setup Platform/Renderer backends
		ImGui_ImplWin32_Init(m_window);
		ImGui_ImplDX12_Init(
			m_d3dDevice.Get(),
			c_swapBufferCount,
			c_rtvFormat,
			m_srvDescriptorHeap.Get(),
			CD3DX12_CPU_DESCRIPTOR_HANDLE(m_srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), 5, m_cbvSrvDescriptorSize),
			CD3DX12_GPU_DESCRIPTOR_HANDLE(m_srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 5, m_cbvSrvDescriptorSize));

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();
	}

	// ================================================================================================================
	// #07. Setup Physx.
	// ================================================================================================================
	{
		gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);

		gPvd = PxCreatePvd(*gFoundation);
		PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate(PVD_HOST, 5425, 10);
		gPvd->connect(*transport, PxPvdInstrumentationFlag::eALL);

		gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, PxTolerancesScale(), true, gPvd);

		PxSceneDesc sceneDesc(gPhysics->getTolerancesScale());
		sceneDesc.gravity = PxVec3(0.0f, -9.81f, 0.0f);
		gDispatcher = PxDefaultCpuDispatcherCreate(2);
		sceneDesc.cpuDispatcher = gDispatcher;
		sceneDesc.filterShader = PxDefaultSimulationFilterShader;
		gScene = gPhysics->createScene(sceneDesc);

		PxPvdSceneClient* pvdClient = gScene->getScenePvdClient();
		if (pvdClient)
		{
			pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
			pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
			pvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
		}
		gMaterial = gPhysics->createMaterial(0.5f, 0.5f, 0.1f);
	}
}

// Allocate all memory resources that change on a window SizeChanged event.
void Surtr::CreateWindowSizeDependentResources()
{
	// Release resources that are tied to the swap chain and update fence values.
	for (UINT n = 0; n < c_swapBufferCount; n++)
	{
		m_renderTargets[n].Reset();
		m_fenceValues[n] = m_fenceValues[m_backBufferIndex];
	}

	const UINT backBufferWidth = static_cast<UINT>(m_outputWidth);
	const UINT backBufferHeight = static_cast<UINT>(m_outputHeight);

	// ================================================================================================================
	// #01. Create/Resize swap chain.
	// ================================================================================================================
	{
		// If the swap chain already exists, resize it, otherwise create one.
		if (m_swapChain)
		{
			const HRESULT hr = m_swapChain->ResizeBuffers(
				c_swapBufferCount, backBufferWidth, backBufferHeight, c_backBufferFormat, 0);

			if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
			{
				// If the device was removed for any reason, a new device and swap chain will need to be created.
				OnDeviceLost();
				return;
			}

			DX::ThrowIfFailed(hr);
		}
		else
		{
			// If swap chain does not exist, create it.
			DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
			swapChainDesc.Width = backBufferWidth;
			swapChainDesc.Height = backBufferHeight;
			swapChainDesc.Format = c_backBufferFormat;
			swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapChainDesc.BufferCount = c_swapBufferCount;
			swapChainDesc.SampleDesc.Count = 1;
			swapChainDesc.SampleDesc.Quality = 0;
			swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
			swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
			swapChainDesc.Flags = m_fullScreenMode ? 0x0 : DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

			DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
			fsSwapChainDesc.Windowed = TRUE;

			ComPtr<IDXGISwapChain1> swapChain;
			DX::ThrowIfFailed(
				m_dxgiFactory->CreateSwapChainForHwnd(
					m_commandQueue.Get(),
					m_window,
					&swapChainDesc,
					&fsSwapChainDesc,
					nullptr,
					swapChain.GetAddressOf()
				));

			DX::ThrowIfFailed(swapChain.As(&m_swapChain));

			// Prevent alt enter.
			DX::ThrowIfFailed(m_dxgiFactory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER));

			// If fullscreen mode, set fullscreen state and resize buffers.
			if (m_fullScreenMode)
			{
				DX::ThrowIfFailed(m_swapChain->SetFullscreenState(TRUE, NULL));
				DX::ThrowIfFailed(m_swapChain->ResizeBuffers(
					c_swapBufferCount,
					backBufferWidth,
					backBufferHeight,
					c_backBufferFormat,
					0x0
				));
			}
		}

		// Set back buffer index.
		m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
	}

	// ================================================================================================================
	// #02. Create RTV.
	// ================================================================================================================
	{
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

		for (UINT n = 0; n < c_swapBufferCount; n++)
		{
			// Obtain the back buffers for this window which will be the final render targets
			DX::ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(m_renderTargets[n].GetAddressOf())));

			wchar_t name[25] = {};
			swprintf_s(name, L"Render target %u", n);
			DX::ThrowIfFailed(m_renderTargets[n]->SetName(name));

			const CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
				m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(n), m_rtvDescriptorSize);

			// Create render target views for each of them.
			m_d3dDevice->CreateRenderTargetView(m_renderTargets[n].Get(), &rtvDesc, rtvHandle);
		}
	}

	// ================================================================================================================
	// #03. Create depth/stencil buffer & view.
	// ================================================================================================================
	{
		// Create depth/stencil buffer to default heap.
		const CD3DX12_HEAP_PROPERTIES depthHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
		D3D12_RESOURCE_DESC depthStencilDesc =
			CD3DX12_RESOURCE_DESC::Tex2D(c_depthBufferFormat, backBufferWidth, backBufferHeight, 1, 1);
		depthStencilDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		const CD3DX12_CLEAR_VALUE depthOptimizedClearValue(c_depthBufferFormat, 1.0f, 0u);

		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommittedResource(
				&depthHeapProperties,
				D3D12_HEAP_FLAG_NONE,
				&depthStencilDesc,
				D3D12_RESOURCE_STATE_DEPTH_WRITE,
				&depthOptimizedClearValue,
				IID_PPV_ARGS(m_depthStencil.ReleaseAndGetAddressOf())
			));

		DX::ThrowIfFailed(m_depthStencil->SetName(L"Depth stencil"));

		// Create DSV.
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = c_depthBufferFormat;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

		m_d3dDevice->CreateDepthStencilView(
			m_depthStencil.Get(), &dsvDesc, m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	}

	// ================================================================================================================
	// #04. Set viewport/scissor rect.
	// ================================================================================================================
	{
		m_viewport.TopLeftX = 0.0f;
		m_viewport.TopLeftY = 0.0f;
		m_viewport.Width = static_cast<float>(backBufferWidth);
		m_viewport.Height = static_cast<float>(backBufferHeight);
		m_viewport.MinDepth = D3D12_MIN_DEPTH;
		m_viewport.MaxDepth = D3D12_MAX_DEPTH;

		m_scissorRect.left = 0;
		m_scissorRect.top = 0;
		m_scissorRect.right = static_cast<LONG>(backBufferWidth);
		m_scissorRect.bottom = static_cast<LONG>(backBufferHeight);
	}
}

void Surtr::CreateCommandListDependentResources()
{
	// ----------> Prepare command list.
	DX::ThrowIfFailed(m_commandAllocators[m_backBufferIndex]->Reset());
	DX::ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_backBufferIndex].Get(), nullptr));

	// ================================================================================================================
	// #01. Create structured buffer.
	// ================================================================================================================
	{
		CD3DX12_HEAP_PROPERTIES uploadHeapProp(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(c_nSBCnt * sizeof(MeshSB));
		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommittedResource(
				&uploadHeapProp,
				D3D12_HEAP_FLAG_NONE,
				&resDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(m_sbUploadHeap.ReleaseAndGetAddressOf())));

		// Create SRV.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = c_nSBCnt;
		srvDesc.Buffer.StructureByteStride = sizeof(MeshSB);
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		m_d3dDevice->CreateShaderResourceView(m_sbUploadHeap.Get(), &srvDesc, m_srvDescriptorHeapSB->GetCPUDescriptorHandleForHeapStart());
	}

	// Pre-declare upload heap.
	// Because they must be alive until GPU work (upload) is done.
	ComPtr<ID3D12Resource> textureUploadHeaps[4];

	// ================================================================================================================
	// #02. Create texture resources & views.
	// ================================================================================================================
	{
		CreateTextureResource(
			L"Resources\\Textures\\dummy.dds",
			m_colorLTexResource.ReleaseAndGetAddressOf(),
			textureUploadHeaps[0].ReleaseAndGetAddressOf(),
			0);
		CreateTextureResource(
			L"Resources\\Textures\\dummy.dds",
			m_colorRTexResource.ReleaseAndGetAddressOf(),
			textureUploadHeaps[1].ReleaseAndGetAddressOf(),
			1);
		CreateTextureResource(
			L"Resources\\Textures\\dummy.dds",
			m_heightLTexResource.ReleaseAndGetAddressOf(),
			textureUploadHeaps[2].ReleaseAndGetAddressOf(),
			2);
		CreateTextureResource(
			L"Resources\\Textures\\dummy.dds",
			m_heightRTexResource.ReleaseAndGetAddressOf(),
			textureUploadHeaps[3].ReleaseAndGetAddressOf(),
			3);
	}

	// ================================================================================================================
	// #03. Load model vertices and indices.
	// ================================================================================================================

	std::vector<VertexNormalColor> objectVertexData;
	std::vector<uint32_t> objectIndexData;

	switch (m_modelIndex)
	{
	case 0:
		LoadModelData("Resources\\Models\\lowpoly-bunny-closed.obj", XMFLOAT3(70, 70, 70), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
		break;
	case 1:
		LoadModelData("Resources\\Models\\cube.obj", XMFLOAT3(3, 3, 3), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
		break;
	case 2:
		LoadModelData("Resources\\Models\\pumpkin.obj", XMFLOAT3(0.15, 0.15, 0.15), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
		break;
	case 3:
		LoadModelData("Resources\\Models\\cylinder.obj", XMFLOAT3(3, 3, 3), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
		break;
	case 4:
		LoadModelData("Resources\\Models\\highpoly-sphere.obj", XMFLOAT3(5, 5, 5), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
		break;
	case 5:
		LoadModelData("Resources\\Models\\cessna.obj", XMFLOAT3(0.6, 0.6, 0.6), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
		break;
	case 6:
		LoadModelData("Resources\\Models\\shuttle.obj", XMFLOAT3(1, 1, 1), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
		break;
	}

	// Allocate Dynamic Mesh Pool.
	for (int i = 0; i < c_nDynamicMeshPoolCnt; i++)
	{
		DynamicMesh* mesh = new DynamicMesh();

		mesh->AllocatedVBSize = sizeof(VertexNormalColor) * objectVertexData.size();
		mesh->AllocatedIBSize = sizeof(uint32_t) * objectIndexData.size();

		mesh->AllocateVB(m_d3dDevice.Get());
		mesh->AllocateIB(m_d3dDevice.Get());

		m_dynamicMeshPool.push(mesh);
	}

	m_initCompoundTask = [this](const Piece* piece, const Extract* extract, bool renderConvex) -> std::pair<PxConvexMeshGeometry, DynamicMesh*>
	{
		std::vector<VertexNormalColor> vertexData;
		std::vector<uint32_t> indexData;

		if (TRUE == renderConvex)
			Poly::RenderPolyhedron(vertexData, indexData, piece->Convex, extract, true);
		else
			Poly::RenderPolyhedron(vertexData, indexData, piece->Mesh, Poly::ExtractFaces(piece->Mesh), false);

		return std::make_pair(CookingConvex(piece, extract), PrepareDynamicMeshResource(vertexData, indexData, true));
	};

	m_refittingTask = [this](Piece* piece) -> void
	{
		Kdop::KdopContainer kdop(GenerateICHNormal(piece->Mesh, std::min((int)piece->Mesh.size(), m_fractureArgs.RefittingPointLimit)));
		kdop.Calc(piece->Mesh);

		piece->Convex = kdop.ClipWithPolyhedron(piece->Convex);
	};

	m_fractureTask = [this](const VMACH::Polygon3D& voroPoly, const std::vector<Piece*>& targetPieceVec, const std::set<int>& outside) -> std::vector<Piece*>
	{
		std::vector<Piece*> localDecompose;

		for (int c = 0; c < targetPieceVec.size(); c++)
		{
			if (TRUE == outside.contains(c))
				continue;

			Poly::Polyhedron convex = Poly::ClipPolyhedron(targetPieceVec[c]->Convex, voroPoly);
			if (convex.empty())
				continue;

			Poly::Polyhedron mesh = Poly::ClipPolyhedron(targetPieceVec[c]->Mesh, voroPoly);
			if (mesh.empty())
				continue;

			const auto groupVec = CheckMeshIsland(mesh);
			if (groupVec.size() >= 2)
			{
				for (const auto& group : groupVec)
				{
					Poly::Polyhedron island;
					std::unordered_map<int, int> mapping;

					for (const int iVert : group)
					{
						int oldIndex = island.size();
						mapping[iVert] = oldIndex;

						island.push_back(mesh[iVert]);
					}

					for (auto& vert : island)
						for (int& iAdj : vert.NeighborVertexVec)
							iAdj = mapping[iAdj];

					localDecompose.push_back(new Piece(convex, island));
				}
			}
			else
			{
				localDecompose.push_back(new Piece(convex, mesh));
			}
		}

		return localDecompose;
	};

	// Sphere point cloud.
	{
		LoadModelData("Resources\\Models\\sphere.obj", XMFLOAT3(0.5, 0.5, 0.5), XMFLOAT3(0, 0, 0), m_sphereVertexData, m_sphereIndexData);

		m_spherePointCloud = std::vector<Vector3>(m_sphereVertexData.size());
		std::transform(m_sphereVertexData.begin(), 
					   m_sphereVertexData.end(), 
					   m_spherePointCloud.begin(), 
					   [](const VertexNormalColor& vnc) { return vnc.Position; });

		m_impactPointMesh = PrepareDynamicMeshResource(m_sphereVertexData, m_sphereIndexData);
	}

	// Set ground rigidbody.
	{
		std::vector<VertexNormalColor> groundVertexData;
		std::vector<uint32_t> groundIndexData;
		LoadModelData("Resources\\Models\\ground.obj", XMFLOAT3(0.015f, 0.015f, 0.015f), XMFLOAT3(0, -2, 0), groundVertexData, groundIndexData);

		m_groundMesh = PrepareMeshResource(groundVertexData, groundIndexData);

		PxRigidStatic* groundRigidBody = PxCreatePlane(*gPhysics, PxPlane(0, 1, 0, 2), *gMaterial);
		gScene->addActor(*groundRigidBody);
	}

	// Set initial compound.
	{
		Compound initialCompound = PrepareFracture(objectVertexData, objectIndexData);
		InitCompound(initialCompound, false, PxVec3(0, 5, 0));
	}

	{
		std::vector<VertexNormalColor> vertices;
		std::vector<uint32_t> indices;
		const auto cube = Poly::GetBB();

		Poly::RenderPolyhedron(vertices, indices, cube, Poly::ExtractFaces(cube), true, Vector3(0, 1, 0));
		m_patternBoundaryMesh = PrepareDynamicMeshResource(vertices, indices);
	}

	// Upload structured data.
	UploadStructuredBuffer();

	// <---------- Close command list.
	DX::ThrowIfFailed(m_commandList->Close());
	m_commandQueue->ExecuteCommandLists(1, CommandListCast(m_commandList.GetAddressOf()));

	WaitForGpu();

	// Release no longer needed upload heaps.
	for (int i = 0; i < 4; i++)
		textureUploadHeaps[i].Reset();
}

void Surtr::WaitForGpu() noexcept
{
	if (m_commandQueue && m_fence && m_fenceEvent.IsValid())
	{
		// Schedule a Signal command in the GPU queue.
		const UINT64 fenceValue = m_fenceValues[m_backBufferIndex];
		if (SUCCEEDED(m_commandQueue->Signal(m_fence.Get(), fenceValue)))
		{
			// Wait until the Signal has been processed.
			if (SUCCEEDED(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent.Get())))
			{
				std::ignore = WaitForSingleObjectEx(m_fenceEvent.Get(), INFINITE, FALSE);

				// Increment the fence value for the current frame.
				m_fenceValues[m_backBufferIndex]++;
			}
		}
	}
}

void Surtr::MoveToNextFrame()
{
	// Schedule a Signal command in the queue.
	const UINT64 currentFenceValue = m_fenceValues[m_backBufferIndex];
	DX::ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

	// Update the back buffer index.
	m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_backBufferIndex])
	{
		DX::ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_backBufferIndex], m_fenceEvent.Get()));
		std::ignore = WaitForSingleObjectEx(m_fenceEvent.Get(), INFINITE, FALSE);
	}

	// Set the fence value for the next frame.
	m_fenceValues[m_backBufferIndex] = currentFenceValue + 1;
}

// This method acquires the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, try WARP. Otherwise throw an exception.
void Surtr::GetAdapter(IDXGIAdapter1** ppAdapter) const
{
	*ppAdapter = nullptr;

	ComPtr<IDXGIAdapter1> adapter;
	for (UINT adapterIndex = 0;
		DXGI_ERROR_NOT_FOUND != m_dxgiFactory->EnumAdapters1(adapterIndex, adapter.ReleaseAndGetAddressOf());
		++adapterIndex)
	{
		DXGI_ADAPTER_DESC1 desc;
		DX::ThrowIfFailed(adapter->GetDesc1(&desc));

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// Don't select the Basic Render Driver adapter.
			continue;
		}

		// Check to see if the adapter supports Direct3D 12, but don't create the actual device yet.
		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), m_featureLevel, __uuidof(ID3D12Device), nullptr)))
		{
			break;
		}
	}

#if !defined(NDEBUG)
	if (!adapter)
	{
		if (FAILED(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()))))
		{
			throw std::runtime_error("WARP12 not available. Enable the 'Graphics Tools' optional feature");
		}
	}
#endif

	if (!adapter)
	{
		throw std::runtime_error("No Direct3D 12 device found");
	}

	*ppAdapter = adapter.Detach();
}

void Surtr::OnDeviceLost()
{
	// imgui
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	// Physx
	PX_RELEASE(gScene);
	PX_RELEASE(gDispatcher);
	PX_RELEASE(gPhysics);
	if (gPvd)
	{
		PxPvdTransport* transport = gPvd->getTransport();
		gPvd->release();	gPvd = NULL;
		PX_RELEASE(transport);
	}
	PX_RELEASE(gFoundation);

	// Shadow
	m_shadowMap.reset();

	// Meshes
	for (auto& meshes : m_fractureStorage.CompoundMeshVec)
		for (MeshBase* mesh : meshes)
			if (mesh != nullptr)
				delete mesh;

	delete m_patternBoundaryMesh;
	delete m_groundMesh;
	delete m_impactPointMesh;

	// Dynamic mesh pools.
	while (FALSE == m_dynamicMeshPool.empty())
	{
		DynamicMesh* mesh = m_dynamicMeshPool.front();
		if (mesh != nullptr)
			delete mesh;
		m_dynamicMeshPool.pop();
	}

	// Compounds.
	for (auto& compound : m_fractureStorage.CompoundVec)
	{
		for (Piece* piece : compound.PieceVec)
			if (piece != nullptr)
				delete piece;

		for (Extract* extract : compound.PieceExtractedConvex)
			if (extract != nullptr)
				delete extract;
	}

	// Textures
	m_colorLTexResource.Reset();
	m_colorRTexResource.Reset();
	m_heightLTexResource.Reset();
	m_heightRTexResource.Reset();

	// Resources
	m_swapChain.Reset();
	for (UINT n = 0; n < c_swapBufferCount; n++)
		m_renderTargets[n].Reset();
	m_depthStencil.Reset();

	// Shadow map
	m_shadowMap.reset();

	// SB
	m_sbUploadHeap.Reset();

	// CB
	m_cbOpaqueUploadHeap.Reset();
	m_cbShadowUploadHeap.Reset();
	m_cbOpaqueMappedData = nullptr;
	m_cbShadowMappedData = nullptr;

	// Descriptor heaps
	m_rtvDescriptorHeap.Reset();
	m_dsvDescriptorHeap.Reset();
	m_srvDescriptorHeap.Reset();
	m_srvDescriptorHeapSB.Reset();

	// Command objects
	m_commandQueue.Reset();
	for (UINT n = 0; n < c_swapBufferCount; n++)
		m_commandAllocators[n].Reset();
	m_commandList.Reset();

	// Fence objects
	m_fence.Reset();

	// Device resources
	m_dxgiFactory.Reset();
	m_d3dDevice.Reset();

	CreateDeviceResources();
	CreateDeviceDependentResources();
	CreateWindowSizeDependentResources();
	CreateCommandListDependentResources();
}

Surtr::Compound Surtr::PrepareFracture(_In_ const std::vector<VertexNormalColor>& visualMeshVertices, _In_ const std::vector<uint32_t>& visualMeshIndices)
{
	// 1. Create intermediate convex hull with limit count.
	std::vector<Vector3> vertices(visualMeshVertices.size());
	std::transform(visualMeshVertices.begin(), visualMeshVertices.end(), vertices.begin(), [](const VertexNormalColor& vertex) { return vertex.Position; });

	// 2. Collect ICH face normals.
	std::vector<Vector3> ichFaceNormalVec = GenerateICHNormal(vertices, m_fractureArgs.ICHIncludePointLimit);
	m_fractureResult.ICHFaceCnt = ichFaceNormalVec.size();

	// 3. Calculate bounding box.
	double minX, maxX, minY, maxY, minZ, maxZ;
	{
		const auto x = std::minmax_element(vertices.begin(), vertices.end(), [](const Vector3& p1, const Vector3& p2) { return p1.x < p2.x; });
		const auto y = std::minmax_element(vertices.begin(), vertices.end(), [](const Vector3& p1, const Vector3& p2) { return p1.y < p2.y; });
		const auto z = std::minmax_element(vertices.begin(), vertices.end(), [](const Vector3& p1, const Vector3& p2) { return p1.z < p2.z; });

		minX = (*x.first).x;    maxX = (*x.second).x;
		minY = (*y.first).y;    maxY = (*y.second).y;
		minZ = (*z.first).z;    maxZ = (*z.second).z;
	}

	m_fractureStorage.BBCenter = Vector3((maxX + minX) / 2.0, (maxY + minY) / 2.0, (maxZ + minZ) / 2.0);
	m_fractureStorage.MinBB = Vector3(minX, minY, minZ);
	m_fractureStorage.MaxBB = Vector3(maxX, maxY, maxZ);
	m_fractureStorage.MaxAxisScale = std::max(std::max(maxX - minX, maxY - minY), maxZ - minZ);

	// 4. Calculate min/max plane for k-DOP generation.
	Kdop::KdopContainer achKdop(ichFaceNormalVec);
	achKdop.Calc(vertices, m_fractureStorage.MaxAxisScale, m_fractureArgs.ACHPlaneGapInverse);

	// 5. Init bounding box polygon.
	Poly::Polyhedron achPolyhedron = Poly::GetBB();
	Poly::Scale(achPolyhedron, Vector3((maxX - minX), (maxY - minY), (maxZ - minZ)));
	Poly::Scale(achPolyhedron, Vector3(2.0, 2.0, 2.0));
	Poly::Translate(achPolyhedron, m_fractureStorage.BBCenter);

	// 6. Clip ACH polygon with clipping faces.
	achPolyhedron = achKdop.ClipWithPolyhedron(achPolyhedron);

	// 7. Init Mesh Polygon.
	Poly::Polyhedron meshPolyhedron;
	{
		std::vector<int> indices(visualMeshIndices.size());
		std::transform(visualMeshIndices.begin(), visualMeshIndices.end(), indices.begin(), [](const uint32_t i) { return (int)i; });

		const std::vector<std::vector<int>> nei = Poly::ExtractNeighborFromMesh(vertices, indices);
		Poly::InitPolyhedron(meshPolyhedron, vertices, nei);
	}

	// 8. Voronoi diagram generation for initial decomposition.
	std::vector<VMACH::Polygon3D> voroPolyVec = GenerateVoronoi(m_fractureArgs.InitialDecomposeCellCnt);
	for (VMACH::Polygon3D& voro : voroPolyVec)
	{
		voro.Scale(Vector3((maxX - minX), (maxY - minY), (maxZ - minZ)));
		voro.Translate(m_fractureStorage.BBCenter);
	}

	// 9. Generate Fracture Pattern.
	m_fractureStorage.PartialFracturePattern = GenerateFracturePattern(m_fractureArgs.PartialFracturePatternCellCnt, m_fractureArgs.PartialFracturePatternDist);
	m_fractureStorage.GeneralFracturePattern = GenerateFracturePattern(m_fractureArgs.GeneralFracturePatternCellCnt, m_fractureArgs.GeneralFracturePatternDist);

	// 10. Generate initial pieces.
	Compound preCompound = Compound({ new Piece(achPolyhedron, meshPolyhedron) }, { Poly::ExtractFaces(achPolyhedron) });
	CompoundInfo initial = ApplyFracture(preCompound, voroPolyVec, m_spherePointCloud);
	
	Refitting(initial.PieceVec);
	SetExtract(initial);

	Compound result;
	for (const auto& iComp : initial.CompoundBind)
	{
		for (const int iPiece : iComp)
		{
			result.PieceVec.push_back(initial.PieceVec[iPiece]);
			result.PieceExtractedConvex.push_back(initial.PieceExtractedConvex[iPiece]);
		}
	}

	return result;
}

void Surtr::ExecuteFractureRoutine(physx::PxRigidActor* targetRididBody)
{
	auto itr = std::find(m_fractureStorage.RigidDynamicVec.begin(), m_fractureStorage.RigidDynamicVec.end(), targetRididBody);
	if (itr != m_fractureStorage.RigidDynamicVec.end())
	{
		TIMER_INIT;
		TIMER_START;

		int targetIndex = std::distance(m_fractureStorage.RigidDynamicVec.begin(), itr);

		// Get world transform matrix range.
		int startID = 0;
		for (int i = 0; i < targetIndex; i++)
			startID += m_fractureStorage.CompoundVec[i].PieceVec.size();
		int endID = startID + m_fractureStorage.CompoundVec[targetIndex].PieceVec.size() - 1;

		// Apply world transform matrix to target compound.
		for (int j = 0; j < m_fractureStorage.CompoundVec[targetIndex].PieceVec.size(); j++)
		{
			Piece* piece = m_fractureStorage.CompoundVec[targetIndex].PieceVec[j];
			Poly::Transform(piece->Convex, m_structuredBufferData[startID + j].WorldMatrix);
			Poly::Transform(piece->Mesh, m_structuredBufferData[startID + j].WorldMatrix);
		}

		// Do fracture.
		std::vector<Compound> fracturedCompoundVec = DoFracture(m_fractureStorage.CompoundVec[targetIndex]);

		// Destroy target rigidbody.
		gScene->removeActor(*targetRididBody);
		m_fractureStorage.RigidDynamicVec.erase(std::next(m_fractureStorage.RigidDynamicVec.begin(), targetIndex));

		// Destroy target mesh. Also Re-cycle mesh buffer.
		for (DynamicMesh* mesh : m_fractureStorage.CompoundMeshVec[targetIndex])
		{
			mesh->Clean();
			m_dynamicMeshPool.push(mesh);
		}
		m_fractureStorage.CompoundMeshVec.erase(std::next(m_fractureStorage.CompoundMeshVec.begin(), targetIndex));

		// Destroy piece data.
		m_fractureStorage.CompoundVec.erase(std::next(m_fractureStorage.CompoundVec.begin(), targetIndex));

		// Remove world matrix of target compound mesh.
		m_structuredBufferData.erase(std::next(m_structuredBufferData.begin(), startID), std::next(m_structuredBufferData.begin(), endID + 1));

		for (Compound& compound : fracturedCompoundVec)
			InitCompound(compound, false);

		OutputDebugStringWFormat(L"\n\nTotal Elapsed: ");
		TIMER_STOP_PRINT;
		OutputDebugStringWFormat(L"\n\n");
	}
	else
		OutputDebugStringW(L"Impact point is not valid!\n");
}

std::vector<Surtr::Compound> Surtr::DoFracture(const Compound& targetCompound)
{
	std::vector<VMACH::Polygon3D> localFracturePattern = m_fractureArgs.PartialFracture ? m_fractureStorage.PartialFracturePattern : m_fractureStorage.GeneralFracturePattern;
	std::vector<Vector3> localSpherePointCloud = m_spherePointCloud;

	// Scale.
	for (VMACH::Polygon3D& voro : localFracturePattern)
		voro.Scale(Vector3(m_fractureStorage.MaxAxisScale, m_fractureStorage.MaxAxisScale, m_fractureStorage.MaxAxisScale) * 2);

	// Alignment.
	for (VMACH::Polygon3D& voro : localFracturePattern)
		voro.Translate(m_fractureArgs.ImpactPosition);

	{
		Poly::Polyhedron cube = Poly::GetBB();
		Poly::Scale(cube, Vector3(m_fractureStorage.MaxAxisScale, m_fractureStorage.MaxAxisScale, m_fractureStorage.MaxAxisScale) * 2);
		Poly::Translate(cube, m_fractureArgs.ImpactPosition);

		std::vector<VertexNormalColor> vertexData;
		std::vector<uint32_t> indexData;
		Poly::RenderPolyhedron(vertexData, indexData, cube, Poly::ExtractFaces(cube), true, Vector3(0, 1, 0));

		UpdateDynamicMesh(m_patternBoundaryMesh, vertexData, indexData);
	}

	// Align sphere point cloud.
	for (auto& v : localSpherePointCloud)
	{
		v *= m_fractureArgs.ImpactRadius;
		v += m_fractureArgs.ImpactPosition;
	}

	TIMER_INIT;
	TIMER_START_NAME(L"ApplyFracture\t\t");

	// 11. Apply fracture pattern.
	CompoundInfo second = ApplyFracture(targetCompound, localFracturePattern, localSpherePointCloud, m_fractureArgs.PartialFracture);
	SetExtract(second);

	TIMER_STOP_PRINT;
	TIMER_START_NAME(L"MergeOutOfImpact\t\t");

	if (TRUE == m_fractureArgs.PartialFracture)
		MergeOutOfImpact(second, localSpherePointCloud);

	TIMER_STOP_PRINT;
	TIMER_START_NAME(L"HandleConvexIsland\t\t");

	HandleConvexIsland(second);

	TIMER_STOP_PRINT;
	TIMER_START_NAME(L"Refitting\t\t");

	Refitting(second.PieceVec);
	SetExtract(second);

	TIMER_STOP_PRINT;

	std::vector<Compound> result;
	for (const auto& iComp : second.CompoundBind)
	{
		std::vector<Piece*> pieceVec;
		std::vector<Extract*> extractVec;

		for (const int iPiece : iComp)
		{
			pieceVec.push_back(second.PieceVec[iPiece]);
			extractVec.push_back(second.PieceExtractedConvex[iPiece]);
		}

		result.push_back(Compound(pieceVec, extractVec));
	}

	return result;
}

std::vector<Vector3> Surtr::GenerateICHNormal(_In_ const std::vector<Vector3>& vertices, _In_ const int ichIncludePointLimit) const
{
	VMACH::ConvexHull ich(vertices, ichIncludePointLimit);

	std::vector<Vector3> ichFaceNormalVec;
	for (const VMACH::ConvexHullFace& f : ich.GetFaces())
	{
		Vector3 normal = (f.Vertices[1] - f.Vertices[0]).Cross(f.Vertices[2] - f.Vertices[0]);
		normal.Normalize();
		ichFaceNormalVec.push_back(normal);
	}

	return ichFaceNormalVec;
}

std::vector<Vector3> Surtr::GenerateICHNormal(_In_ const Poly::Polyhedron& polyhedron, _In_ const int ichIncludePointLimit) const
{
	std::vector<Vector3> vertices(polyhedron.size());
	std::transform(polyhedron.begin(), polyhedron.end(), vertices.begin(), [](const Poly::Vertex& vert) { return vert.Position; });

	return GenerateICHNormal(vertices, ichIncludePointLimit);
}

std::vector<VMACH::Polygon3D> Surtr::GenerateVoronoi(_In_ const int cellCount) const
{
	std::vector<Vector3> cellPointVec;

	std::mt19937 gen(m_fractureArgs.Seed);
	std::uniform_real_distribution<double> uniformDist(-0.5, 0.5);

	double x, y, z;
	for (int i = 0; i < cellCount; i++)
	{
		x = uniformDist(gen);
		y = uniformDist(gen);
		z = uniformDist(gen);
		cellPointVec.emplace_back(x, y, z);
	}

	return GenerateVoronoi(cellPointVec);
}

std::vector<VMACH::Polygon3D> Surtr::GenerateVoronoi(_In_ const std::vector<Vector3>& cellPointVec) const
{
	std::vector<VMACH::Polygon3D> voroPolyVec;

	voro::container voroCon(
		-0.5, +0.5,
		-0.5, +0.5,
		-0.5, +0.5,
		8, 8, 8, false, false, false, 8);

	for (int i = 0; i < cellPointVec.size(); i++)
		voroCon.put(i, cellPointVec[i].x, cellPointVec[i].y, cellPointVec[i].z);

	int id;
	voro::voronoicell_neighbor voroCell;
	std::vector<int> neighborVec;

	voro::c_loop_all cl(voroCon);
	int dimension = 0;
	if (cl.start()) do if (voroCon.compute_cell(voroCell, cl))
	{
		dimension += 1;
	} while (cl.inc());

	double x, y, z;
	int counter = 0;
	if (cl.start()) do if (voroCon.compute_cell(voroCell, cl))
	{
		cl.pos(x, y, z);
		id = cl.pid();

		std::vector<int> cellFaceVec;
		std::vector<double> cellVertices;

		voroCell.neighbors(neighborVec);
		voroCell.face_vertices(cellFaceVec);

		voroCell.vertices(x, y, z, cellVertices);

		VMACH::Polygon3D voroPoly = { true };

		int cur = 0;
		while (cur < cellFaceVec.size())
		{
			VMACH::PolygonFace face = { true };

			int cnt = cellFaceVec[cur];
			for (int i = 0; i < cnt; i++)
			{
				int vertIndex = cellFaceVec[cur + i + 1];
				face.AddVertex(Vector3(
					cellVertices[3 * vertIndex],
					cellVertices[3 * vertIndex + 1],
					cellVertices[3 * vertIndex + 2]));
			}
			cur += cnt + 1;

			face.Rewind();
			voroPoly.AddFace(face);
		}

		voroPolyVec.push_back(voroPoly);

		counter += 1;
	} while (cl.inc());

	return voroPolyVec;
}

std::vector<VMACH::Polygon3D> Surtr::GenerateFracturePattern(_In_ const int cellCount, _In_ const double mean) const
{
	std::vector<Vector3> cellPointVec;

	std::mt19937 gen(m_fractureArgs.Seed);
	std::uniform_real_distribution<double> directionUniformDist(-1.0, 1.0);
	std::exponential_distribution<double> lengthExpDist(1.0 / mean);

	for (int i = 0; i < cellCount; i++)
	{
		double len = std::max(std::min(lengthExpDist(gen), 0.5), 1e-12);

		double x = directionUniformDist(gen);
		double y = directionUniformDist(gen);
		double z = directionUniformDist(gen);

		Vector3 v = Vector3(x, y, z);
		v.Normalize();
		v *= len;

		cellPointVec.push_back(v);
	}

	return GenerateVoronoi(cellPointVec);
}

Surtr::CompoundInfo Surtr::ApplyFracture(_In_ const Compound& compound,
										 _In_ const std::vector<VMACH::Polygon3D>& voroPolyVec,
										 _In_ const std::vector<Vector3>& spherePointCloud,
										 _In_ bool partial) const
{
	std::vector<Piece*> decompose;
	std::vector<std::set<int>> bind;

	const std::vector<Piece*>& targetPieceVec = compound.PieceVec;
	const std::vector<Extract*>& extractVec = compound.PieceExtractedConvex;

	// Check convex located at outside or not.
	std::set<int> outside;
	std::set<int> outsideBind;
	if (TRUE == partial)
	{
		for (int c = 0; c < targetPieceVec.size(); c++)
		{
			if (TRUE == ConvexOutOfSphere(targetPieceVec[c]->Convex, extractVec[c], spherePointCloud, m_fractureArgs.ImpactPosition, m_fractureArgs.ImpactRadius))
			{
				outside.insert(c);

				outsideBind.insert(decompose.size());
				decompose.push_back(targetPieceVec[c]);
			}
		}
	}

	// 0-th element is reserved.
	bind.push_back(outsideBind);

	std::vector<std::future<std::vector<Piece*>>> futures;
	for (int i = 0; i < voroPolyVec.size(); i++)
		futures.push_back(g_threadPool.enqueue(m_fractureTask, voroPolyVec[i], targetPieceVec, outside));

	for (int i = 0; i < futures.size(); i++)
	{
		std::vector<Piece*> localDecompose = futures[i].get();

		int offset = decompose.size();
		decompose.insert(decompose.end(), localDecompose.begin(), localDecompose.end());
		
		std::set<int> localBind;
		for (int x = offset; x < offset + localDecompose.size(); x++)
			localBind.insert(x);

		if (FALSE == localBind.empty())
			bind.push_back(localBind);
	}

	return CompoundInfo(decompose, {}, bind);
}

void Surtr::SetExtract(_Inout_ CompoundInfo& preResult) const
{
	preResult.PieceExtractedConvex.resize(preResult.PieceVec.size(), nullptr);
	std::transform(preResult.PieceVec.begin(), preResult.PieceVec.end(), preResult.PieceExtractedConvex.begin(), [](const Piece* p) { return Poly::ExtractFaces(p->Convex); });
}

void Surtr::_MeshIslandLoop(const int index, const Poly::Polyhedron& mesh, std::set<int>& group) const
{
	std::vector<int> search;
	for (const int iAdj : mesh[index].NeighborVertexVec)
	{
		const auto res = group.insert(iAdj);
		if (TRUE == res.second)
			search.push_back(iAdj);
	}

	for (const int iSearch : search)
		_MeshIslandLoop(iSearch, mesh, group);
}

std::vector<std::set<int>> Surtr::CheckMeshIsland(_In_ const Poly::Polyhedron& polyhedron) const
{
	std::vector<std::set<int>> groupVec;
	std::set<int> exclude;

	int iArbitPoint = 0;
	while (TRUE)
	{
		std::set<int> group;
		_MeshIslandLoop(iArbitPoint, polyhedron, group);

		groupVec.push_back(group);
		exclude.insert(group.begin(), group.end());

		bool remain = false;
		for (int v = 0; v < polyhedron.size(); v++)
		{
			if (FALSE == exclude.contains(v))
			{
				remain = true;
				iArbitPoint = v;
				break;
			}
		}

		if (FALSE == remain)
			break;
	}

	return groupVec;
}

void Surtr::HandleConvexIsland(_Inout_ CompoundInfo& compoundInfo) const
{
	// FaceNode struct is only needed for this function.
	struct FaceNode
	{
		int						CID;
		double					AbsD;
		Plane					FacePlane;
		std::vector<Vector3>	FacePoints;
	};

	std::vector<std::set<int>> newBind;

	for (auto& localBind : compoundInfo.CompoundBind)
	{
		if (localBind.size() <= 1)
			continue;

		std::vector<FaceNode> nodes;
		for (const int cid : localBind)
		{
			for (const auto& poly : *compoundInfo.PieceExtractedConvex[cid])
			{
				std::vector<Vector3> points(poly.size());
				std::transform(poly.begin(), poly.end(), points.begin(), [&](const int v) { return compoundInfo.PieceVec[cid]->Convex[v].Position; });

				Plane p(points[0], points[1], points[2]);
				nodes.push_back(FaceNode(cid, std::abs(p.D()), p, points));
			}
		}

		std::sort(nodes.begin(), nodes.end(), [](const FaceNode& a, const FaceNode& b) { return a.AbsD < b.AbsD; });

		std::unordered_map<int, std::set<int>> nei;
		for (const int cid : localBind)
			nei[cid] = std::set<int>();

		for (int i = 0; i < nodes.size() - 1; i++)
		{
			bool lowerBoundFound = false;
			for (int j = i + 1; j < nodes.size(); j++)
			{
				if (TRUE == lowerBoundFound && (nodes[i].AbsD > nodes[j].AbsD))
					break;

				// Approximatly check planes are equal or not.
				if (std::abs(nodes[i].AbsD - nodes[j].AbsD) > 1e-3)
					continue;

				lowerBoundFound = true;

				Vector3 in = nodes[i].FacePlane.Normal();
				Vector3 jn = nodes[j].FacePlane.Normal();
				in.Normalize(); jn.Normalize();

				// Normals should be opposite.
				bool oppositeNormal = std::abs(1 + in.Dot(jn)) < 1e-4;
				if (FALSE == oppositeNormal)
					continue;

				// Check two faces are intersect or not.
				bool atLeastOnePointIncluded = false;

				// First, check vertex of i node is contained by j node.
				int nJPoint = nodes[j].FacePoints.size();
				for (const Vector3& iPoint : nodes[i].FacePoints)
				{
					bool pointIncluded = true;
					for (int v = 0; v < nJPoint; v++)
					{
						const Vector3& a = nodes[j].FacePoints[v];
						const Vector3& b = nodes[j].FacePoints[(v + 1) % nJPoint];

						if (FALSE == VMACH::OnYourRight(a, b, iPoint, jn))
						{
							pointIncluded = false;
							break;
						}
					}

					if (TRUE == pointIncluded)
					{
						atLeastOnePointIncluded = true;
						break;
					}
				}

				// Give me one more chance.
				if (FALSE == atLeastOnePointIncluded)
				{
					// If no point included, check vertex of j node is contained by i node.
					int nIPoint = nodes[i].FacePoints.size();
					for (const Vector3& jPoint : nodes[j].FacePoints)
					{
						bool pointIncluded = true;
						for (int v = 0; v < nIPoint; v++)
						{
							const Vector3& a = nodes[i].FacePoints[v];
							const Vector3& b = nodes[i].FacePoints[(v + 1) % nIPoint];

							if (FALSE == VMACH::OnYourRight(a, b, jPoint, in))
							{
								pointIncluded = false;
								break;
							}
						}

						if (TRUE == pointIncluded)
						{
							atLeastOnePointIncluded = true;
							break;
						}
					}
				}

				// If all condition met, they are neighbors.
				if (TRUE == atLeastOnePointIncluded)
				{
					nei[nodes[i].CID].insert(nodes[j].CID);
					nei[nodes[j].CID].insert(nodes[i].CID);
				}
			}
		}

		// Flood fill.
		std::set<int> remain;
		remain.insert(localBind.begin(), localBind.end());

		std::vector<std::set<int>> splitGroup;

		while (remain.size() != 0)
		{
			std::set<int> split;

			std::queue<int> fillQueue;
			fillQueue.push(*remain.begin());

			while (FALSE == fillQueue.empty())
			{
				int curr = fillQueue.front();
				fillQueue.pop();

				if (TRUE == remain.contains(curr))
				{
					split.insert(curr);
					remain.erase(curr);

					for (const int iAdj : nei[curr])
						fillQueue.push(iAdj);
				}
			}

			splitGroup.push_back(split);
		}

		if (splitGroup.size() >= 2)
		{
			localBind = splitGroup[0];
			newBind.insert(newBind.end(), std::next(splitGroup.begin()), splitGroup.end());
		}
	}

	compoundInfo.CompoundBind.insert(compoundInfo.CompoundBind.end(), newBind.begin(), newBind.end());
}

void Surtr::MergeOutOfImpact(_Inout_ CompoundInfo& compoundInfo, _In_ const std::vector<Vector3>& spherePointCloud) const
{
	std::set<int> emptyCompound;

	// Ignore 0-th element.
	for (int i = 1; i < compoundInfo.CompoundBind.size(); i++)
	{
		auto& local = compoundInfo.CompoundBind[i];

		std::set<int> outside;
		for (const int c : local)
		{
			if (TRUE == ConvexOutOfSphere(compoundInfo.PieceVec[c]->Convex, compoundInfo.PieceExtractedConvex[c], spherePointCloud, m_fractureArgs.ImpactPosition, m_fractureArgs.ImpactRadius))
				outside.insert(c);
		}

		if (FALSE == outside.empty())
		{
			std::set<int> tempLocal;
			std::set_difference(local.begin(), local.end(), outside.begin(), outside.end(), std::inserter(tempLocal, tempLocal.end()));
			local.swap(tempLocal);

			if (TRUE == local.empty())
				emptyCompound.insert(i);

			compoundInfo.CompoundBind[0].insert(outside.begin(), outside.end());
		}
	}

	// Clean size 0.
	compoundInfo.CompoundBind.erase(
		std::remove_if(std::next(compoundInfo.CompoundBind.begin()),
					   compoundInfo.CompoundBind.end(),
					   [](const auto& local) { return local.empty(); }),
		compoundInfo.CompoundBind.end());
}

void Surtr::Refitting(_Inout_ std::vector<Piece*>& targetPieceVec) const
{
	std::vector<std::future<void>> futures;
	for (int i = 0; i < targetPieceVec.size(); i++)
		futures.push_back(g_threadPool.enqueue(m_refittingTask, targetPieceVec[i]));

	for (int i = 0; i < futures.size(); i++)
		futures[i].get();
}

bool Surtr::ConvexOutOfSphere(_In_ const Poly::Polyhedron& polyhedron,
							  _In_ const Extract* extract,
							  _In_ const std::vector<Vector3>& spherePointCloud,
							  _In_ const Vector3 origin,
							  _In_ const float radius) const
{
	// Approximate.
	bool noVertexInsideSphere = true;
	for (const auto& v : polyhedron)
	{
		if ((origin - v.Position).Length() < radius)
		{
			noVertexInsideSphere = false;
			break;
		}
	}

	if (FALSE == noVertexInsideSphere)
		return false;

	for (const auto& po : spherePointCloud)
	{
		bool contain = true;
		for (const auto& f : *extract)
		{
			Vector3 normal = (polyhedron[f[1]].Position - polyhedron[f[0]].Position).Cross(polyhedron[f[2]].Position - polyhedron[f[0]].Position);
			normal.Normalize();

			float d = -polyhedron[f[0]].Position.Dot(normal);

			float dist = normal.Dot(po) + d;
			if (dist > 0)
			{
				contain = false;
				break;
			}
		}

		if (TRUE == contain)
			return false;
	}

	return true;
}

bool Surtr::ConvexRayIntersection(_In_ const VMACH::Polygon3D& convex, _In_ const Ray ray, _Out_ float& dist) const
{
	float minDist = std::numeric_limits<float>::max();
	bool hit = false;

	for (const auto& face : convex.FaceVec)
	{
		float d;
		if (FALSE == ray.Intersects(face.FacePlane, d))
			continue;

		Vector3 intersectionPoint = ray.position + ray.direction * d;
		int nVert = face.VertexVec.size();
		const Vector3 faceNormal = face.GetNormal();

		bool inside = true;
		for (int v = 0; v < nVert; v++)
		{
			const Vector3& a = face.VertexVec[v];
			const Vector3& b = face.VertexVec[(v + 1) % nVert];

			if (FALSE == VMACH::OnYourRight(a, b, intersectionPoint, faceNormal))
			{
				inside = false;
				break;
			}
		}

		if (TRUE == inside && d < minDist)
		{
			minDist = d;
			hit = true;
		}
	}

	dist = minDist;
	return hit;
}

void Surtr::InitCompound(const Compound& compound, bool renderConvex, const physx::PxVec3 translate)
{
	PxRigidDynamic* compoundRigidBody = gPhysics->createRigidDynamic(PxTransform(translate));

	std::vector<std::future<std::pair<PxConvexMeshGeometry, DynamicMesh*>>> futures;
	for (int i = 0; i < compound.PieceVec.size(); i++)
		futures.push_back(g_threadPool.enqueue(m_initCompoundTask, compound.PieceVec[i], compound.PieceExtractedConvex[i], renderConvex));

	std::vector<DynamicMesh*> meshes(futures.size());
	for (int i = 0; i < futures.size(); i++)
	{
		const auto result = futures[i].get();

		if (result.first.convexMesh != nullptr)
			PxShape* convexShape = PxRigidActorExt::createExclusiveShape(*compoundRigidBody, result.first, *gMaterial);
		
		meshes[i] = result.second;
	}

	PxRigidBodyExt::updateMassAndInertia(*compoundRigidBody, 10.0f);
	gScene->addActor(*compoundRigidBody);

	// #TODO : HOT-SPOT! 0.5ms
	m_fractureStorage.CompoundVec.push_back(compound);

	m_fractureStorage.RigidDynamicVec.push_back(compoundRigidBody);
	m_fractureStorage.CompoundMeshVec.push_back(meshes);
	m_structuredBufferData.resize(m_structuredBufferData.size() + meshes.size(), MeshSB(XMMatrixIdentity()));
}

PxConvexMeshGeometry Surtr::CookingConvex(const Piece* piece, const Extract* extract)
{
	std::vector<PxVec3> convexVertexData(piece->Convex.size());
	std::transform(piece->Convex.begin(),
				   piece->Convex.end(),
				   convexVertexData.begin(),
				   [](const Poly::Vertex& vert) { return PxVec3(vert.Position.x, vert.Position.y, vert.Position.z); });

	PxConvexMeshDesc convexDesc;
	convexDesc.points.count = convexVertexData.size();
	convexDesc.points.stride = sizeof(PxVec3);
	convexDesc.points.data = convexVertexData.data();
	convexDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX | PxConvexFlag::eDISABLE_MESH_VALIDATION | PxConvexFlag::ePLANE_SHIFTING;

	PxTolerancesScale scale;
	PxCookingParams params(scale);
	params.planeTolerance = 0.000007f;
	params.meshPreprocessParams = PxMeshPreprocessingFlag::eWELD_VERTICES;

	PxConvexMesh* convexMesh = PxCreateConvexMesh(params, convexDesc, gPhysics->getPhysicsInsertionCallback());
	
	return PxConvexMeshGeometry(convexMesh, PxMeshScale(), PxConvexMeshGeometryFlag::eTIGHT_BOUNDS);
}

PxConvexMeshGeometry Surtr::CookingConvexManual(const Poly::Polyhedron& polyhedron, const std::vector<std::vector<int>>& extract)
{
	// #TODO : Currently not working.
	std::vector<PxVec3> convexVertexData(polyhedron.size());
	std::transform(polyhedron.begin(), 
				   polyhedron.end(), 
				   convexVertexData.begin(), 
				   [](const Poly::Vertex& vert) { return PxVec3(vert.Position.x, vert.Position.y, vert.Position.z); });

	std::vector<PxU16> convexIndexData;
	std::vector<PxHullPolygon> convexPolygonData;

	PxU16 offset = 0;
	for (const auto& f : extract)
	{
		PxHullPolygon hullPoly;
		hullPoly.mNbVerts = f.size();
		hullPoly.mIndexBase = offset;

		PxPlane plane(convexVertexData[f[0]], convexVertexData[f[1]], convexVertexData[f[2]]);
		hullPoly.mPlane[0] = plane.n.x;
		hullPoly.mPlane[1] = plane.n.y;
		hullPoly.mPlane[2] = plane.n.z;
		hullPoly.mPlane[3] = plane.d;

		convexPolygonData.push_back(hullPoly);

		for (const int i : f)
			convexIndexData.push_back(i);

		offset += f.size();
	}

	PxConvexMeshDesc convexDesc;
	convexDesc.points.count = convexVertexData.size();
	convexDesc.points.stride = sizeof(PxVec3);
	convexDesc.points.data = convexVertexData.data();

	convexDesc.indices.count = convexIndexData.size();
	convexDesc.indices.stride = sizeof(PxU16);
	convexDesc.indices.data = convexIndexData.data();

	convexDesc.polygons.count = convexPolygonData.size();
	convexDesc.polygons.stride = sizeof(PxHullPolygon);
	convexDesc.polygons.data = convexPolygonData.data();

	convexDesc.flags = PxConvexFlag::e16_BIT_INDICES | PxConvexFlag::ePLANE_SHIFTING;

	PxTolerancesScale scale;
	PxCookingParams params(scale);

	PxDefaultMemoryOutputStream buf;
	if (!PxCookConvexMesh(params, convexDesc, buf))
		throw std::exception();

	PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
	PxConvexMesh* convexMesh = gPhysics->createConvexMesh(input);

	return PxConvexMeshGeometry(convexMesh, PxMeshScale(), PxConvexMeshGeometryFlag::eTIGHT_BOUNDS);
}

void Surtr::SetRigidBodyDebugValue(PxRigidActor* rigidBody, const uint32_t debugValue)
{
	auto itr = std::find(m_fractureStorage.RigidDynamicVec.begin(), m_fractureStorage.RigidDynamicVec.end(), rigidBody);
	if (itr != m_fractureStorage.RigidDynamicVec.end())
	{
		int targetIndex = std::distance(m_fractureStorage.RigidDynamicVec.begin(), itr);
		std::vector<DynamicMesh*>& targetMeshVec = m_fractureStorage.CompoundMeshVec[targetIndex];
		for (DynamicMesh* mesh : targetMeshVec)
			mesh->DebugValue = debugValue;
	}
}

void Surtr::CreateTextureResource(
	_In_ const wchar_t* fileName,
	_Out_ ID3D12Resource** texture,
	_In_ ID3D12Resource** uploadHeap,
	_In_ UINT index) const
{
	std::unique_ptr<uint8_t[]> ddsData;
	std::vector<D3D12_SUBRESOURCE_DATA> subResourceDataVec;

	// Load DDS texture.
	DX::ThrowIfFailed(
		LoadDDSTextureFromFile(
			m_d3dDevice.Get(), fileName,
			texture, ddsData, subResourceDataVec));

	// Create SRV.
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = (*texture)->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = (*texture)->GetDesc().MipLevels;

	const CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
		m_srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), index, m_cbvSrvDescriptorSize);
	m_d3dDevice->CreateShaderResourceView(*texture, &srvDesc, srvHandle);

	// Calculate upload buffer size.
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(*texture, 0, static_cast<UINT>(subResourceDataVec.size()));

	// Create upload heap.
	CD3DX12_HEAP_PROPERTIES uploadHeapProp(D3D12_HEAP_TYPE_UPLOAD);
	auto uploadHeapDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
	DX::ThrowIfFailed(
		m_d3dDevice->CreateCommittedResource(
			&uploadHeapProp,
			D3D12_HEAP_FLAG_NONE,
			&uploadHeapDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(uploadHeap)));

	// Upload resources.
	UpdateSubresources(
		m_commandList.Get(), *texture, *uploadHeap, 0, 0,
		static_cast<UINT>(subResourceDataVec.size()), subResourceDataVec.data());

	// Translate state.
	const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		*texture,
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_commandList->ResourceBarrier(1, &barrier);
}

void Surtr::LoadModelData(_In_ const std::string fileName,
						  _In_ DirectX::XMFLOAT3 scale,
						  _In_ DirectX::XMFLOAT3 translate,
						  _Out_ std::vector<VertexNormalColor>& vertices,
						  _Out_ std::vector<uint32_t>& indices)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(fileName,
		aiProcess_CalcTangentSpace |
		aiProcess_Triangulate |
		aiProcess_FlipWindingOrder |
		aiProcess_JoinIdenticalVertices);

	if (scene == nullptr)
		throw std::exception();

	vertices = std::vector<VertexNormalColor>();
	indices = std::vector<uint32_t>();

	for (int i = 0; i < scene->mMeshes[0]->mNumVertices; i++)
	{
		aiVector3D v, n, t;

		if (scene->mMeshes[0]->mVertices != nullptr)
			v = scene->mMeshes[0]->mVertices[i];

		if (scene->mMeshes[0]->mNormals != nullptr)
			n = scene->mMeshes[0]->mNormals[i];

		vertices.push_back(
			VertexNormalColor(
				XMFLOAT3(-v.x * scale.x + translate.x, v.y * scale.y + translate.y, v.z * scale.z + translate.z),
				XMFLOAT3(-n.x, n.y, n.z)));
	}

	for (int i = 0; i < scene->mMeshes[0]->mNumFaces; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			indices.push_back(scene->mMeshes[0]->mFaces[i].mIndices[j]);
		}
	}

	OutputDebugStringWFormat(L"Mesh Vertex: %d / Mesh Index: %d\n", vertices.size(), indices.size());
}

StaticMesh* Surtr::PrepareMeshResource(_In_ const std::vector<VertexNormalColor>& vertices, _In_ const std::vector<uint32_t>& indices)
{
	StaticMesh* staticMesh = new StaticMesh(vertices, indices);

	// Prepare vertex buffer.
	{
		// Create default heap.
		CD3DX12_HEAP_PROPERTIES defaultHeapProp(D3D12_HEAP_TYPE_DEFAULT);
		auto resDesc = CD3DX12_RESOURCE_DESC::Buffer(staticMesh->VBSize);
		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommittedResource(
				&defaultHeapProp,
				D3D12_HEAP_FLAG_NONE,
				&resDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(staticMesh->VB.ReleaseAndGetAddressOf())));

		// Initialize vertex buffer view.
		staticMesh->VBV.BufferLocation = staticMesh->VB->GetGPUVirtualAddress();
		staticMesh->VBV.StrideInBytes = sizeof(VertexNormalColor);
		staticMesh->VBV.SizeInBytes = staticMesh->VBSize;

		// Create upload heap.
		CD3DX12_HEAP_PROPERTIES uploadHeapProp(D3D12_HEAP_TYPE_UPLOAD);
		auto uploadHeapDesc = CD3DX12_RESOURCE_DESC::Buffer(staticMesh->VBSize);
		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommittedResource(
				&uploadHeapProp,
				D3D12_HEAP_FLAG_NONE,
				&uploadHeapDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(staticMesh->VertexUploadHeap.ReleaseAndGetAddressOf())));

		// Define sub-resource data.
		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = vertices.data();
		subResourceData.RowPitch = staticMesh->VBSize;
		subResourceData.SlicePitch = staticMesh->VBSize;

		// Copy the vertex data to the default heap.
		UpdateSubresources(m_commandList.Get(), staticMesh->VB.Get(), staticMesh->VertexUploadHeap.Get(), 0, 0, 1, &subResourceData);

		// Translate vertex buffer state.
		const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			staticMesh->VB.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		m_commandList->ResourceBarrier(1, &barrier);
	}

	// Prepare index buffer.
	{
		// Create default heap.
		CD3DX12_HEAP_PROPERTIES defaultHeapProp(D3D12_HEAP_TYPE_DEFAULT);
		auto resDesc = CD3DX12_RESOURCE_DESC::Buffer(staticMesh->IBSize);
		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommittedResource(
				&defaultHeapProp,
				D3D12_HEAP_FLAG_NONE,
				&resDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(staticMesh->IB.ReleaseAndGetAddressOf())));

		// Initialize index buffer view.
		staticMesh->IBV.BufferLocation = staticMesh->IB->GetGPUVirtualAddress();
		staticMesh->IBV.Format = DXGI_FORMAT_R32_UINT;
		staticMesh->IBV.SizeInBytes = staticMesh->IBSize;

		// Create upload heap.
		CD3DX12_HEAP_PROPERTIES uploadHeapProp(D3D12_HEAP_TYPE_UPLOAD);
		auto uploadHeapDesc = CD3DX12_RESOURCE_DESC::Buffer(staticMesh->IBSize);
		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommittedResource(
				&uploadHeapProp,
				D3D12_HEAP_FLAG_NONE,
				&uploadHeapDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(staticMesh->IndexUploadHeap.ReleaseAndGetAddressOf())));

		// Define sub-resource data.
		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = indices.data();
		subResourceData.RowPitch = staticMesh->IBSize;
		subResourceData.SlicePitch = staticMesh->IBSize;

		// Copy the vertex data to the default heap.
		UpdateSubresources(m_commandList.Get(), staticMesh->IB.Get(), staticMesh->IndexUploadHeap.Get(), 0, 0, 1, &subResourceData);

		// Translate vertex buffer state.
		const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			staticMesh->IB.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		m_commandList->ResourceBarrier(1, &barrier);
	}

	return staticMesh;
}

DynamicMesh* Surtr::PrepareDynamicMeshResource(_In_ const std::vector<VertexNormalColor>& vertices, _In_ const std::vector<uint32_t>& indices, bool usePool)
{
	if (TRUE == usePool && FALSE == m_dynamicMeshPool.empty())
	{
		DynamicMesh* dynamicMesh = m_dynamicMeshPool.front();
		m_dynamicMeshPool.pop();

		UpdateDynamicMesh(dynamicMesh, vertices, indices);

		return dynamicMesh;
	}

	DynamicMesh* dynamicMesh = new DynamicMesh(vertices, indices);

	// Prepare vertex buffer.
	dynamicMesh->AllocateVB(m_d3dDevice.Get());
	dynamicMesh->UploadVB();

	// Prepare index buffer.
	dynamicMesh->AllocateIB(m_d3dDevice.Get());
	dynamicMesh->UploadIB();

	return dynamicMesh;
}

void Surtr::UpdateDynamicMesh(_Inout_ DynamicMesh* dynamicMesh, _In_ const std::vector<VertexNormalColor>& vertices, _In_ const std::vector<uint32_t>& indices)
{
	dynamicMesh->UpdateMeshData(vertices, indices);

	if (dynamicMesh->RenderVBSize > dynamicMesh->AllocatedVBSize)
	{
		dynamicMesh->AllocatedVBSize = dynamicMesh->RenderVBSize;

		// Re-Allocation.
		dynamicMesh->AllocateVB(m_d3dDevice.Get());
	}

	if (dynamicMesh->RenderIBSize > dynamicMesh->AllocatedIBSize)
	{
		dynamicMesh->AllocatedIBSize = dynamicMesh->RenderIBSize;

		// Re-Allocation.
		dynamicMesh->AllocateIB(m_d3dDevice.Get());
	}

	dynamicMesh->UploadVB();
	dynamicMesh->UploadIB();
}
