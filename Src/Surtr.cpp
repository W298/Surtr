#include "pch.h"
#include "Surtr.h"

#include "voro++.hh"
#include "DT.h"
#include "DT3D.h"

extern void ExitGame() noexcept;

using namespace DirectX;
using namespace SimpleMath;
using Microsoft::WRL::ComPtr;
using Poly::Polyhedron;

const auto rnd = []() { return double(rand() * 0.75) / RAND_MAX; };

Surtr::Surtr() noexcept :
    m_window(nullptr),
    m_outputWidth(1280),
    m_outputHeight(720),
    m_backBufferIndex(0),
    m_rtvDescriptorSize(0),
	m_dsvDescriptorSize(0),
	m_cbvSrvDescriptorSize(0),
    m_featureLevel(D3D_FEATURE_LEVEL_11_0),
    m_fenceValues{}
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

void meshIslandLoop(const int index, const Polyhedron& vm, std::set<int>& group)
{
    std::vector<int> search;
	for (const int iAdj : vm[index].NeighborVertexVec)
	{
		const auto res = group.insert(iAdj);
		if (TRUE == res.second)
            search.push_back(iAdj);
	}

    for (const int iSearch : search)
        meshIslandLoop(iSearch, vm, group);
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
    m_isFlightMode = true;

    m_shadowMapSize = shadowMapSize;

	m_renderShadow = true;
    m_lightRotation = false;
    
    m_executeNextStep = false;

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

    m_worldMatrix = XMMatrixIdentity();
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

	Ray ray(m_camPosition, rayDir);

	bool hit = false;
	float minDist = std::numeric_limits<float>::max();

	for (const VMACH::Polygon3D& convex : m_convexVec)
	{
		float dist;
		if (TRUE == ConvexRayIntersection(convex, ray, dist))
		{
			if (dist < minDist)
			{
				hit = true;
				minDist = dist;
			}
		}
	}

	if (TRUE == hit)
		m_decompositionArgument.ImpactPosition = ray.position + ray.direction * (minDist + 0.01);
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
	m_projectionMatrix = XMMatrixPerspectiveFovLH(
		XM_PIDIV4,
		m_aspectRatio,
		0.01f,
		1000);

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
}

void Surtr::UpdateMesh()
{
    if (m_executeNextStep)
    {
		// ----------> Prepare command list.
		DX::ThrowIfFailed(m_commandAllocators[m_backBufferIndex]->Reset());
		DX::ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_backBufferIndex].Get(), nullptr));

		auto achVertexData = std::vector<VertexNormalColor>();
		auto achIndexData = std::vector<uint32_t>();

        DoFracture(m_meshVec[0]->VertexData, m_meshVec[0]->IndexData, achVertexData, achIndexData);
        
        delete m_meshVec[2];
        m_meshVec[2] = PrepareMeshResource(achVertexData, achIndexData);

        m_executeNextStep = false;

		// <---------- Close command list.
		DX::ThrowIfFailed(m_commandList->Close());
		m_commandQueue->ExecuteCommandLists(1, CommandListCast(m_commandList.GetAddressOf()));
    }

    WaitForGpu();
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

    // Update mesh data if needed.
    UpdateMesh();
    
    // ----------> Prepare command list.
    DX::ThrowIfFailed(m_commandAllocators[m_backBufferIndex]->Reset());
    DX::ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_backBufferIndex].Get(), nullptr));

    // Set descriptor heaps.
    m_commandList->SetDescriptorHeaps(1, m_srvDescriptorHeap.GetAddressOf());

    // Set root signature & descriptor table.
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
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
			
            for (int i = 0; i < m_meshVec.size(); i++)
			{
				if (Mesh::RenderOptionType::NOT_RENDER ^ m_meshVec[i]->RenderOption)
					m_meshVec[i]->Render(m_commandList.Get());
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

            cbOpaque.WorldMatrix = XMMatrixTranspose(m_worldMatrix);
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

			for (int i = 0; i < m_meshVec.size(); i++)
			{
				if (Mesh::RenderOptionType::SOLID & m_meshVec[i]->RenderOption)
					m_meshVec[i]->Render(m_commandList.Get());
			}

			m_commandList->SetPipelineState(m_wireframePSO.Get());

			for (int i = 0; i < m_meshVec.size(); i++)
			{
                if ((Mesh::RenderOptionType::WIREFRAME & m_meshVec[i]->RenderOption) && (Mesh::RenderOptionType::SOLID & m_meshVec[i]->RenderOption))
                    m_meshVec[i]->Render(m_commandList.Get());
			}

            m_commandList->SetPipelineState(m_coloredWireframePSO.Get());

			for (int i = 0; i < m_meshVec.size(); i++)
			{
				if ((Mesh::RenderOptionType::WIREFRAME & m_meshVec[i]->RenderOption) && !(Mesh::RenderOptionType::SOLID & m_meshVec[i]->RenderOption))
					m_meshVec[i]->Render(m_commandList.Get());
			}

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

                    ImGui::Text("Mesh Vertex Count: %d", m_meshVec[0]->VertexCount);

                    ImGui::Dummy(ImVec2(0.0f, 20.0f));

                    ImGui::Text("[Arguments]");
                    ImGui::SliderInt("ICH Included Points", &m_decompositionArgument.ICHIncludePointLimit, 20, 1000);
                    ImGui::SliderFloat("ACH Plane Gap Inverse", &m_decompositionArgument.ACHPlaneGapInverse, 0.0f, 10000.0f);
                    ImGui::SliderInt("Seed", &m_decompositionArgument.Seed, 0, 100000);
                    ImGui::SliderFloat("Impact Radius", &m_decompositionArgument.ImpactRadius, 0.1f, 10.0f);
                    ImGui::Text("Impact Point: %.3f %.3f %.3f", m_decompositionArgument.ImpactPosition.x, m_decompositionArgument.ImpactPosition.y, m_decompositionArgument.ImpactPosition.z);
                    if (ImGui::Button("Regenerate!")) { m_executeNextStep = true; }

                    ImGui::Dummy(ImVec2(0.0f, 10.0f));

                    ImGui::Text("[Results]");
                    ImGui::Text("ICH Face Count: %d", m_decompositionResult.ICHFaceCnt);
                    
                    if (m_decompositionResult.ACHErrorPointCnt == 0)
                        ImGui::TextColored(ImVec4(0, 1, 0, 1), "ALL VERTEX CONTAINED");
                    else
                        ImGui::TextColored(ImVec4(1, 0, 0, 1), "%d VERTEX NOT CONTAINED!", m_decompositionResult.ACHErrorPointCnt);

                    ImGui::Dummy(ImVec2(0.0f, 20.0f));

                    ImGui::SliderFloat("Rotate speed", &m_camRotateSpeed, 0.0f, 1.0f);
                    ImGui::Text("Move speed: %.3f (Scroll to Adjust)", m_camMoveSpeed);

                    ImGui::Dummy(ImVec2(0.0f, 20.0f));

                    ImGui::Checkbox("Rotate Light", &m_lightRotation);
                    ImGui::Checkbox("Render Shadow", &m_renderShadow);

                    ImGui::Dummy(ImVec2(0.0f, 10.0f));

					ImGui::Text("Object Render Mode");
					if (ImGui::Button("NOT_RENDER"))
						m_meshVec[0]->RenderOption = Mesh::RenderOptionType::NOT_RENDER;
					ImGui::SameLine();
					if (ImGui::Button("SOLID"))
						m_meshVec[0]->RenderOption = Mesh::RenderOptionType::SOLID;
					ImGui::SameLine();
					if (ImGui::Button("WIREFRAME"))
						m_meshVec[0]->RenderOption = Mesh::RenderOptionType::WIREFRAME;
					ImGui::SameLine();
					if (ImGui::Button("BOTH"))
						m_meshVec[0]->RenderOption = Mesh::RenderOptionType::SOLID | Mesh::RenderOptionType::WIREFRAME;

                    ImGui::Text("Convex Render Mode");
					if (ImGui::Button("_NOT_RENDER"))
						m_meshVec[2]->RenderOption = Mesh::RenderOptionType::NOT_RENDER;
					ImGui::SameLine();
					if (ImGui::Button("_SOLID"))
						m_meshVec[2]->RenderOption = Mesh::RenderOptionType::SOLID;
					ImGui::SameLine();
					if (ImGui::Button("_WIREFRAME"))
						m_meshVec[2]->RenderOption = Mesh::RenderOptionType::WIREFRAME;
					ImGui::SameLine();
					if (ImGui::Button("_BOTH"))
						m_meshVec[2]->RenderOption = Mesh::RenderOptionType::SOLID | Mesh::RenderOptionType::WIREFRAME;

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
    }

    // ================================================================================================================
    // #02. Create root signature.
    // ================================================================================================================
    {
        // Define root parameters.
        CD3DX12_DESCRIPTOR_RANGE srvTable;
        srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0);

        CD3DX12_ROOT_PARAMETER rootParameters[3] = {};
        rootParameters[0].InitAsDescriptorTable(1, &srvTable);  // register (t0)
        rootParameters[1].InitAsConstantBufferView(0);          // register (c0)
        rootParameters[2].InitAsConstantBufferView(1);          // register (c1)

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

    // Pre-declare upload heap.
    // Because they must be alive until GPU work (upload) is done.
    ComPtr<ID3D12Resource> textureUploadHeaps[4];

    // ================================================================================================================
    // #01. Create texture resources & views.
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
    // #02. Load model vertices and indices.
    // ================================================================================================================
	
    std::vector<VertexNormalColor> objectVertexData;
    std::vector<uint32_t> objectIndexData;

    switch (m_modelIndex)
    {
    case 0:
        LoadModelData("Resources\\Models\\lucy.obj", XMFLOAT3(50, 50, 50), XMFLOAT3(0, 20, 0), objectVertexData, objectIndexData);
        break;
    case 1:
        LoadModelData("Resources\\Models\\stanford-bunny.obj", XMFLOAT3(70, 70, 70), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
        break;
    case 2:
        LoadModelData("Resources\\Models\\lowpoly-bunny-closed.obj", XMFLOAT3(70, 70, 70), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
        break;
    case 3:
        LoadModelData("Resources\\Models\\cube.obj", XMFLOAT3(10, 10, 10), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
        break;
    case 4:
        LoadModelData("Resources\\Models\\pump.obj", XMFLOAT3(0.15, 0.15, 0.15), XMFLOAT3(0, 4, -0.85), objectVertexData, objectIndexData);
        break;
    case 5:
        LoadModelData("Resources\\Models\\cylinder.obj", XMFLOAT3(10, 10, 10), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
        break;
    default:
        LoadModelData("Resources\\Models\\lowpoly-bunny-closed.obj", XMFLOAT3(70, 70, 70), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
        break;
    }

	std::vector<VertexNormalColor> sphv;
	std::vector<uint32_t> sphi;
    LoadModelData("Resources\\Models\\sphere.obj", XMFLOAT3(1, 1, 1), XMFLOAT3(0, 0, 0), sphv, sphi);

    m_spherePointCloud = std::vector<Vector3>(sphv.size());
    std::transform(sphv.begin(), sphv.end(), m_spherePointCloud.begin(), [](const VertexNormalColor& vnc) { return vnc.Position; });
    
    Mesh* objectModel = PrepareMeshResource(objectVertexData, objectIndexData);
    m_meshVec.push_back(objectModel);

	std::vector<VertexNormalColor> groundVertexData;
	std::vector<uint32_t> groundIndexData;
    LoadModelData("Resources\\Models\\ground.obj", XMFLOAT3(0.015f, 0.015f, 0.015f), XMFLOAT3(0, -5, 0), groundVertexData, groundIndexData);
    
    Mesh* groundModel = PrepareMeshResource(groundVertexData, groundIndexData);
    m_meshVec.push_back(groundModel);

	// ================================================================================================================
	// #03. Create Approximate Convex Hull with VMACH (Volume Maximize Approximate Convex Hull) method.
	// ================================================================================================================

	std::vector<VertexNormalColor> convexHullVertexData;
	std::vector<uint32_t> convexHullIndexData;
    DoFracture(objectVertexData, objectIndexData, convexHullVertexData, convexHullIndexData);

	if (convexHullVertexData.size() > 0 && convexHullIndexData.size() > 0)
	{
		Mesh* bbModel = PrepareMeshResource(
			convexHullVertexData,
			convexHullIndexData);

		m_meshVec.push_back(bbModel);
	}

    // <---------- Close command list.
    DX::ThrowIfFailed(m_commandList->Close());
    m_commandQueue->ExecuteCommandLists(1, CommandListCast(m_commandList.GetAddressOf()));

    WaitForGpu();

    // TestACHCreation(objectVertexData);
    // TestECHCreation(objectVertexData);

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

    // Shadow
    m_shadowMap.reset();

    // Meshes
    for (int i = 0; i < m_meshVec.size(); i++)
        delete m_meshVec[i];

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

    // CB
    m_cbOpaqueUploadHeap.Reset();
    m_cbShadowUploadHeap.Reset();
    m_cbOpaqueMappedData = nullptr;
    m_cbShadowMappedData = nullptr;

    // Descriptor heaps
    m_rtvDescriptorHeap.Reset();
    m_dsvDescriptorHeap.Reset();
    m_srvDescriptorHeap.Reset();

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

void Surtr::DoFracture(
    _In_ const std::vector<VertexNormalColor>& visualMeshVertices, 
    _In_ const std::vector<uint32_t>& visualMeshIndices, 
    _Out_ std::vector<VertexNormalColor>& achVertexData, 
    _Out_ std::vector<uint32_t>& achIndexData)
{
    achVertexData = std::vector<VertexNormalColor>();
    achIndexData = std::vector<uint32_t>();

    const auto getBB = []()
    {
		std::vector<Vector3> cube_points =
		{ Vector3(-0.5,-0.5,-0.5),  Vector3(+0.5,-0.5,-0.5),  Vector3(+0.5,+0.5,-0.5),  Vector3(-0.5,+0.5,-0.5),
		  Vector3(-0.5,-0.5,+0.5),  Vector3(+0.5,-0.5,+0.5),  Vector3(+0.5,+0.5,+0.5),  Vector3(-0.5,+0.5,+0.5) };
		const std::vector<std::vector<int>> cube_neighbors = { {1, 4, 3},
															  {5, 0, 2},
															  {3, 6, 1},
															  {7, 2, 0},
															  {5, 7, 0},
															  {1, 6, 4},
															  {5, 2, 7},
															  {4, 6, 3} };

		std::vector<Poly::Vertex> poly;
        Poly::InitPolyhedron(poly, cube_points, cube_neighbors);

        return poly;
    };

    const auto translate = [](Polyhedron& poly, const Vector3& v)
    {
        for (int i = 0; i < poly.size(); i++)
            poly[i].Position += v;
    };

	const auto scale = [](Polyhedron& poly, const Vector3& v)
	{
        for (int i = 0; i < poly.size(); i++)
            poly[i].Position *= v;
	};

    const auto planeToPlane3d = [](const Plane& p)
    {
		double d = -p.D();
		Vector3 n = -p.Normal();
		n.Normalize();

        return Poly::Plane(d, n);
    };

    const auto faceToPlane = [](const VMACH::PolygonFace& f)
    {
        Plane p = f.FacePlane;

        double d = -p.D();
        Vector3 n = -p.Normal();
        n.Normalize();

        return Poly::Plane(d, n);
    };

    const auto renderPolyhedron = [&achVertexData, &achIndexData](const Polyhedron& poly, bool isConvex = true)
    {
		double a, b, c;
		a = rnd(); b = rnd(); c = rnd();
		XMFLOAT3 color(a, b, c);

		const auto faceVec = Poly::ExtractFaces(poly);
		for (int i = 0; i < faceVec.size(); i++)
		{
			VMACH::PolygonFace f = { isConvex };
			for (int j = 0; j < faceVec[i].size(); j++)
                f.AddVertex(poly[faceVec[i][j]].Position);

            if (FALSE == isConvex)
            {
                const Vector3 a = poly[faceVec[i][0]].Position;
                const Vector3 b = poly[faceVec[i][1]].Position;
                const Vector3 c = poly[faceVec[i][2]].Position;

                Vector3 normal = (b - a).Cross(c - a);
                
                if (TRUE == f.IsCCW(normal))
                    normal = -normal;

                f.ManuallySetFacePlane(Plane(a, normal));
            }

			f.Render(achVertexData, achIndexData, color);
		}
    };

	const auto renderPolyhedronWithColor = [&achVertexData, &achIndexData](const Polyhedron& poly, bool isConvex, Vector3 color)
	{
		const auto faceVec = Poly::ExtractFaces(poly);
		for (int i = 0; i < faceVec.size(); i++)
		{
			VMACH::PolygonFace f = { isConvex };
			for (int j = 0; j < faceVec[i].size(); j++)
				f.AddVertex(poly[faceVec[i][j]].Position);

			if (FALSE == isConvex)
			{
				const Vector3 a = poly[faceVec[i][0]].Position;
				const Vector3 b = poly[faceVec[i][1]].Position;
				const Vector3 c = poly[faceVec[i][2]].Position;

				Vector3 normal = (b - a).Cross(c - a);

				if (TRUE == f.IsCCW(normal))
					normal = -normal;

				f.ManuallySetFacePlane(Plane(a, normal));
			}

			f.Render(achVertexData, achIndexData, color);
		}
	};

    const auto clipping = [&](const Polyhedron& polyhedron, const VMACH::Polygon3D& poly3d)
    {
        std::vector<Poly::Plane> planes;
		for (int i = 0; i < poly3d.FaceVec.size(); i++)
            planes.push_back(faceToPlane(poly3d.FaceVec[i]));

        Polyhedron res = polyhedron;
        Poly::ClipPolyhedron(res, planes);

        return res;
    };

	const auto clipping2 = [&](const Polyhedron& polyhedron, const VMACH::Kdop& kdop)
	{
		std::vector<Poly::Plane> planes;
		for (int x = 0; x < kdop.elementVec.size(); x++)
		{
			planes.push_back(planeToPlane3d(kdop.elementVec[x].MinPlane));
			planes.push_back(planeToPlane3d(kdop.elementVec[x].MaxPlane));
		}

        Polyhedron res = polyhedron;
		Poly::ClipPolyhedron(res, planes);

        return res;
	};

    const auto checkMeshIsland = [](const Polyhedron& polyhedron)
    {
		// Check mesh islands.
		std::vector<std::set<int>> groupVec;
		std::set<int> exclude;

		int iArbitPoint = 0;
		while (TRUE)
		{
			std::set<int> group;
            meshIslandLoop(iArbitPoint, polyhedron, group);

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
    };

    const auto renderPolygon = [&achVertexData, &achIndexData] (VMACH::Polygon3D polygon, const Vector3& center, const int scalar)
    {
        if (polygon.FaceVec.size() == 0)
            return;

		double a, b, c;
		a = rnd(); b = rnd(); c = rnd();
		XMFLOAT3 color(a, b, c);

		Vector3 outer = polygon.GetCentroid() - center;
		outer.Normalize();
		outer *= scalar;

        polygon.Translate(outer);
        polygon.Render(achVertexData, achIndexData, color);
    };

    // 1. Create intermediate convex hull with limit count.
    std::vector<Vector3> vertices(visualMeshVertices.size());
    std::transform(visualMeshVertices.begin(), visualMeshVertices.end(), vertices.begin(), [](const VertexNormalColor& vertex) { return vertex.Position; });

    /*{
		VMACH::ConvexHull ich(vertices, m_decompositionArgument.ICHIncludePointLimit);

		std::vector<Vector3> ichFaceNormalVec;
		for (const VMACH::ConvexHullFace& f : ich.GetFaces())
		{
			double a, b, c;
			a = rnd(); b = rnd(); c = rnd();
			XMFLOAT3 color(a, b, c);

            Vector3 normal = (f.Vertices[1] - f.Vertices[0]).Cross(f.Vertices[2] - f.Vertices[0]);
            normal.Normalize();

			achVertexData.push_back(VertexNormalColor(f.Vertices[0], normal, color));
			achVertexData.push_back(VertexNormalColor(f.Vertices[1], normal, color));
			achVertexData.push_back(VertexNormalColor(f.Vertices[2], normal, color));

			achIndexData.push_back(achIndexData.size());
			achIndexData.push_back(achIndexData.size());
			achIndexData.push_back(achIndexData.size());
		}

		return;
    }*/

    // 2. Collect ICH face normals.
    std::vector<Vector3> ichFaceNormalVec = GenerateICHNormal(vertices, m_decompositionArgument.ICHIncludePointLimit);
    m_decompositionResult.ICHFaceCnt = ichFaceNormalVec.size();

    // 3. Calculate bounding box.
    double minX, maxX, minY, maxY, minZ, maxZ;
    {
		const auto x = std::minmax_element(vertices.begin(), vertices.end(),
                                           [](const Vector3 &p1, const Vector3 &p2) { return p1.x < p2.x; });
		const auto y = std::minmax_element(vertices.begin(), vertices.end(),
                                           [](const Vector3 &p1, const Vector3 &p2) { return p1.y < p2.y; });
		const auto z = std::minmax_element(vertices.begin(), vertices.end(),
                                           [](const Vector3 &p1, const Vector3 &p2) { return p1.z < p2.z; });

		minX = (*x.first).x;    maxX = (*x.second).x;
        minY = (*y.first).y;    maxY = (*y.second).y;
        minZ = (*z.first).z;    maxZ = (*z.second).z;
    }

	Vector3 bbCenter((maxX + minX) / 2.0, (maxY + minY) / 2.0, (maxZ + minZ) / 2.0);
    double maxAxisScale = std::max(std::max(maxX - minX, maxY - minY), maxZ - minZ);

    // 4. Calculate min/max plane for k-DOP generation.
    VMACH::Kdop achKdop(ichFaceNormalVec);
    achKdop.Calc(vertices, maxAxisScale, m_decompositionArgument.ACHPlaneGapInverse);

    // 5. Init bounding box polygon.
    Polyhedron achPolyhedron = getBB();
    scale(achPolyhedron, Vector3((maxX - minX), (maxY - minY), (maxZ - minZ)));
    scale(achPolyhedron, Vector3(2.0, 2.0, 2.0));
    translate(achPolyhedron, bbCenter);

    // 6. Clip ACH polygon with clipping faces.
    achPolyhedron = clipping2(achPolyhedron, achKdop);

    // 7. Init Mesh Polygon.
	Polyhedron meshPolyhedron;
	{
		std::vector<int> indices(visualMeshIndices.size());
		std::transform(visualMeshIndices.begin(), visualMeshIndices.end(), indices.begin(), [](const uint32_t i) { return (int)i; });

		std::vector<std::vector<int>> nei = Poly::ExtractNeighborFromMesh(vertices, indices);
		Poly::InitPolyhedron(meshPolyhedron, vertices, nei);
	}

    // 9. Voronoi diagram generation for initial decomposition.
    std::vector<VMACH::Polygon3D> voroPolyVec = GenerateVoronoi(64);
    for (int i = 0; i < voroPolyVec.size(); i++)
    {
        voroPolyVec[i].Scale(Vector3((maxX - minX), (maxY - minY), (maxZ - minZ)));
        voroPolyVec[i].Translate(bbCenter);
    }

	// 11. Generate Fracture Pattern.
	std::vector<VMACH::Polygon3D> fractureVoroPolyVec = GenerateFracturePattern(64, 0.01);
	for (int i = 0; i < fractureVoroPolyVec.size(); i++)
		fractureVoroPolyVec[i].Scale(Vector3(maxAxisScale, maxAxisScale, maxAxisScale) * 2);

	// Alignment.
    for (int i = 0; i < fractureVoroPolyVec.size(); i++)
        fractureVoroPolyVec[i].Translate(m_decompositionArgument.ImpactPosition);

    for (auto& v : m_spherePointCloud)
    {
        v *= m_decompositionArgument.ImpactRadius;
        v += m_decompositionArgument.ImpactPosition;
    }

	{
		VMACH::Polygon3D boxPoly = VMACH::GetBoxPolygon();
		boxPoly.Scale(0.05);
		boxPoly.Translate(m_decompositionArgument.ImpactPosition);
		boxPoly.Render(achVertexData, achIndexData, Vector3(1, 0, 0));
	}

	struct Piece
	{
		Polyhedron Convex;
		Polyhedron Mesh;
	};

    struct DecomposeResult
    {
        std::vector<Piece> PieceVec;
        std::vector<std::vector<std::vector<int>>> PieceExtractedConvex;
        std::vector<std::vector<int>> CompoundBind;
    };

    const auto polyOutsideSphere = [&](const Polyhedron& polyhedron, const std::vector<std::vector<int>>& extract, const Vector3& origin, const float radius)
    {
        /*for (const auto& f : extract)
        {
            Plane p(polyhedron[f[0]].Position, polyhedron[f[1]].Position, polyhedron[f[2]].Position);
            
            Vector3 normal = p.Normal();
            normal.Normalize();

            double d = VMACH::CalcDistanceToPoint(origin, p);
            if (std::abs(d) < radius)
            {
                Vector3 projOrigin;
                if (d < 0)
                {
                    projOrigin = origin + normal * std::abs(d);
                }
                else
                {
                    projOrigin = origin - normal * std::abs(d);
                }

                float projRadius = std::sqrt(radius * radius - d * d);

                bool intersect = false;
                for (int v = 0; v < f.size(); v++)
                {
                    const Vector3& a = polyhedron[f[v]].Position;
                    const Vector3& b = polyhedron[f[(v + 1) % f.size()]].Position;

                    const float ed = std::sqrt((projOrigin - a).LengthSquared() - std::pow((projOrigin - a).Dot(b - a), 2));
                    if (ed <= projRadius)
                    {
                        intersect = true;
                        break;
                    }
                }

                if (TRUE == intersect)
                {
                    return true;
                }
            }
        }

        return false;*/

		bool completlyOutside = true;
		for (const auto& v : polyhedron)
		{
			if ((origin - v.Position).Length() < radius)
			{
				completlyOutside = false;
				break;
			}
		}

        if (FALSE == completlyOutside)
            return false;

		for (const auto& po : m_spherePointCloud)
		{
			bool contain = true;
			for (const auto& f : extract)
			{
				Plane p(polyhedron[f[0]].Position, polyhedron[f[1]].Position, polyhedron[f[2]].Position);
				if (VMACH::CalcDistanceToPoint(po, p) > 0)
				{
					contain = false;
					break;
				}
			}

			if (TRUE == contain)
				return false;
		}

		return true;
    };

    const auto decomposition = [&](const DecomposeResult& preDecompose, const std::vector<VMACH::Polygon3D>& voroPolyVec, bool partial = false)
    {
        std::vector<Piece> decompose;
        std::vector<std::vector<int>> bind;

        const std::vector<Piece>& targetPieceVec = preDecompose.PieceVec;
        const std::vector<std::vector<std::vector<int>>>& extract = preDecompose.PieceExtractedConvex;

        // Check convex located at outside or not.
        std::set<int> outside;
        std::vector<int> outsideBind;
		if (TRUE == partial)
		{
			for (int c = 0; c < targetPieceVec.size(); c++)
			{
				if (TRUE == polyOutsideSphere(targetPieceVec[c].Convex, extract[c], m_decompositionArgument.ImpactPosition, m_decompositionArgument.ImpactRadius))
				{
					outside.insert(c);

					outsideBind.push_back(decompose.size());
					decompose.push_back(targetPieceVec[c]);
				}
			}
		}
        
        // 0-th element is reserved.
        bind.push_back(outsideBind);
		
        for (const auto& voroPoly : voroPolyVec)
		{
            std::vector<int> localBind;
            for (int c = 0; c < targetPieceVec.size(); c++)
            {
                if (TRUE == outside.contains(c))
                    continue;

				Polyhedron convex = clipping(targetPieceVec[c].Convex, voroPoly);
				if (convex.empty())
					continue;

				Poly::Polyhedron mesh = clipping(targetPieceVec[c].Mesh, voroPoly);
				if (mesh.empty())
					continue;

				const auto groupVec = checkMeshIsland(mesh);
				if (groupVec.size() >= 2)
				{
					for (const auto& group : groupVec)
					{
						Polyhedron island;
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

                        localBind.push_back(decompose.size());
                        decompose.push_back({ convex, island });
					}
				}
				else
				{
                    localBind.push_back(decompose.size());
                    decompose.push_back({ convex, mesh });
				}
            }

            if (FALSE == localBind.empty())
                bind.push_back(localBind);
		}

        std::vector<std::vector<std::vector<int>>> extracted(decompose.size());
        std::transform(decompose.begin(), decompose.end(), extracted.begin(), [](const Piece& p) { return Poly::ExtractFaces(p.Convex); });

        return DecomposeResult(decompose, extracted, bind);
    };

    const auto afterCheckOutside = [&](DecomposeResult& decompose)
    {
        std::set<int> surplus;

        // Ignore 0-th element.
        for (int i = 1; i < decompose.CompoundBind.size(); i++)
        {
            auto& local = decompose.CompoundBind[i];

			std::set<int> outside;
			for (const int c : local)
			{
				if (TRUE == polyOutsideSphere(decompose.PieceVec[c].Convex, decompose.PieceExtractedConvex[c], m_decompositionArgument.ImpactPosition, m_decompositionArgument.ImpactRadius))
					outside.insert(c);
			}

            if (FALSE == outside.empty())
            {
                local.erase(std::remove_if(local.begin(), local.end(), [&outside](const int c) { return outside.contains(c); }), local.end());
                if (TRUE == local.empty())
                    surplus.insert(i);

                decompose.CompoundBind[0].insert(decompose.CompoundBind[0].end(), outside.begin(), outside.end());
            }
        }

        // Clean size 0.
        decompose.CompoundBind.erase(
            std::remove_if(std::next(decompose.CompoundBind.begin()), decompose.CompoundBind.end(), [](const auto& local) { return local.empty(); }), decompose.CompoundBind.end());
    };

	const std::vector<Vector3> aa =
	{
		Vector3(1, 0, 0),
		Vector3(0, 1, 0),
		Vector3(0, 0, 1),
		Vector3(1, 1, 0),
		Vector3(0, 1, 1),
		Vector3(1, 0, 1),
		Vector3(1, 1, 1),
	};

    const auto reFitting = [&](std::vector<Piece>& targetPieceVec)
    {
		for (Piece& c : targetPieceVec)
		{
			VMACH::Kdop kdop(GenerateICHNormal(c.Mesh, std::min((int)c.Mesh.size(), 6)));
			kdop.Calc(c.Mesh);

			c.Convex = clipping2(c.Convex, kdop);
		}
    };

    struct FaceNode
    {
        int CID;
        double AbsD;
        Plane FacePlane;
        std::vector<Vector3> FacePoints;
    };

    const auto handleConvexIsland = [&renderPolyhedronWithColor](DecomposeResult& decomposeResult)
    {
        std::vector<std::vector<int>> newBind;

        for (auto& localBind : decomposeResult.CompoundBind)
        {
            if (localBind.size() <= 1)
                continue;

            std::vector<FaceNode> nodes;
            for (const int cid : localBind)
            {
                for (const auto& poly : decomposeResult.PieceExtractedConvex[cid])
                {
                    std::vector<Vector3> points(poly.size());
                    std::transform(poly.begin(), poly.end(), points.begin(), [&](const int v) { return decomposeResult.PieceVec[cid].Convex[v].Position; });

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
                for (int j = i + 1; j < nodes.size(); j++)
                {
                    if (std::abs(nodes[i].AbsD - nodes[j].AbsD) < 1e-3)
                    {
                        Vector3 in = nodes[i].FacePlane.Normal();
                        Vector3 jn = nodes[j].FacePlane.Normal();
                        in.Normalize(); jn.Normalize();

                        // Do additional jobs...
                        /*std::vector<Vector3> iProjVec;
                        for (const Vector3& point : nodes[i].FacePoints)
                        {
                            iProjVec.push_back(point - (point -)
                        }

                        float cos = in.z;
                        float ab = std::sqrt(in.x * in.x + in.y * in.y);
                        float sin = ab;
                        float u1 = in.y / ab;
                        float u2 = -in.x / ab;

                        Vector4 a(cos + u1 * u1 * (1 - cos), u1 * u2 * (1 - cos), u2 * sin, 0);
                        Vector4 b(u1* u2* (1 - cos), cos + u2 * u2 * (1 - cos), -u1 * sin, 0);
                        Vector4 c(-u2 * sin, u1* sin, cos, 0);
                        Vector4 d(0, 0, 0, 1);

                        SimpleMath::Matrix rot(a, b, c, d);*/

                        bool onOpposite = std::abs(1 + in.Dot(jn)) < 1e-4;
                        /*bool onOpposite = in.Dot(jn) < 0;*/
                        if (TRUE == onOpposite)
                        {
                            nei[nodes[i].CID].insert(nodes[j].CID);
                            nei[nodes[j].CID].insert(nodes[i].CID);
                        }
                    }
                }
            }

            // Flood fill.
            std::set<int> remain;
            remain.insert(localBind.begin(), localBind.end());

            std::vector<std::vector<int>> splitGroup;

            while (remain.size() != 0)
            {
                std::vector<int> split;

                std::queue<int> fillQueue;
                fillQueue.push(*remain.begin());

                while (FALSE == fillQueue.empty())
                {
                    int curr = fillQueue.front();
                    fillQueue.pop();

                    if (TRUE == remain.contains(curr))
                    {
                        split.push_back(curr);
                        remain.erase(curr);

                        for (const int iAdj : nei[curr])
                            fillQueue.push(iAdj);
                    }
                }

                splitGroup.push_back(split);
            }

            if (splitGroup.size() >= 2)
            {
                // Check neighboring OK or not...
                /*OutputDebugStringWFormat(L"%d\n", splitGroup.size());

                for (const auto& split : splitGroup)
                {
                    double a, b, c;
                    a = rnd(); b = rnd(); c = rnd();
                    XMFLOAT3 color(a, b, c);

                    for (const int i : split)
                        renderPolyhedronWithColor(decomposeResult.CompoundVec[i].Convex, true, color);
                }*/

                localBind = splitGroup[0];
                newBind.insert(newBind.end(), std::next(splitGroup.begin()), splitGroup.end());
            }
        }

        decomposeResult.CompoundBind.insert(decomposeResult.CompoundBind.end(), newBind.begin(), newBind.end());
    };

    // 10. Generate initial pieces.
    DecomposeResult initial = decomposition(DecomposeResult({ Piece(achPolyhedron, meshPolyhedron) }, { Poly::ExtractFaces(achPolyhedron) }, { { 0 } }), voroPolyVec);
    reFitting(initial.PieceVec);

	// For Collision-Ray.
    for (int p = 0; p < initial.PieceVec.size(); p++)
    {
        const auto& faceVec = initial.PieceExtractedConvex[p];

		VMACH::Polygon3D polygon3D = { true };
		for (int i = 0; i < faceVec.size(); i++)
		{
			VMACH::PolygonFace f = { true };
			for (int j = 0; j < faceVec[i].size(); j++)
				f.AddVertex(initial.PieceVec[p].Convex[faceVec[i][j]].Position);

			polygon3D.AddFace(f);
		}

		m_convexVec.push_back(polygon3D);
    }

    // 12. Apply fracture pattern.
    TIMER_INIT;
    TIMER_START;
    
    constexpr bool partialFracture = true;

    DecomposeResult second = decomposition(initial, fractureVoroPolyVec, partialFracture);
    if (TRUE == partialFracture)
        afterCheckOutside(second);
    handleConvexIsland(second);

    reFitting(second.PieceVec);

    TIMER_STOP_PRINT;

    // Render Loop
    for (int l = 0; l < second.CompoundBind.size(); l++)
    {
		double a, b, c;
		a = rnd(); b = rnd(); c = rnd();
		XMFLOAT3 color(a, b, c);

        if (l == 0)
            color = XMFLOAT3(1, 0.5f, 0);

        Vector3 centroid;
        for (const int i : second.CompoundBind[l])
        {
			for (const Poly::Vertex& v : second.PieceVec[i].Convex)
				centroid += v.Position;
            centroid /= second.PieceVec[i].Convex.size();
        }

        for (const int i : second.CompoundBind[l])
        {
            //translate(second.PieceVec[i].Mesh, Vector3(0, 0, (centroid - bbCenter).z * 2));

            //renderPolyhedronWithColor(second.PieceVec[i].Convex, true, color);
            renderPolyhedronWithColor(second.PieceVec[i].Mesh, false, color);
        }
    }

    VMACH::RenderEdge(achVertexData, achIndexData);
}

std::vector<Vector3> Surtr::GenerateICHNormal(_In_ const std::vector<Vector3> vertices, _In_ const int ichIncludePointLimit)
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

std::vector<Vector3> Surtr::GenerateICHNormal(_In_ const VMACH::Polygon3D polygon, _In_ const int ichIncludePointLimit)
{
    std::vector<Vector3> vertices;
    for (int f = 0; f < polygon.FaceVec.size(); f++)
        vertices.insert(vertices.end(), polygon.FaceVec[f].VertexVec.begin(), polygon.FaceVec[f].VertexVec.end());

    return GenerateICHNormal(vertices, ichIncludePointLimit);
}

std::vector<DirectX::SimpleMath::Vector3> Surtr::GenerateICHNormal(
    _In_ const Poly::Polyhedron polyhedron,                                                          
    _In_ const int ichIncludePointLimit)
{
    std::vector<Vector3> vertices(polyhedron.size());
    std::transform(polyhedron.begin(), polyhedron.end(), vertices.begin(),
                   [](const Poly::Vertex &vert) { return vert.Position; });

    return GenerateICHNormal(vertices, ichIncludePointLimit);
}

std::vector<VMACH::Polygon3D> Surtr::GenerateFracturePattern(_In_ const int cellCount, _In_ const double mean)
{
	std::vector<DirectX::SimpleMath::Vector3> cellPointVec;

	std::mt19937 gen(m_decompositionArgument.Seed);
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

bool Surtr::ConvexRayIntersection(_In_ const VMACH::Polygon3D& convex, _In_ const DirectX::SimpleMath::Ray ray, _Out_ float& dist)
{
    float minDist = std::numeric_limits<float>::max();
    bool hit = false;

    for (const auto & face : convex.FaceVec)
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

std::vector<VMACH::Polygon3D> Surtr::GenerateVoronoi(_In_ const int cellCount)
{
    std::vector<DirectX::SimpleMath::Vector3> cellPointVec;

	std::mt19937 gen(m_decompositionArgument.Seed);
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

std::vector<VMACH::Polygon3D> Surtr::GenerateVoronoi(_In_ const std::vector<DirectX::SimpleMath::Vector3>& cellPointVec)
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

void Surtr::TestACHCreation(_In_ const std::vector<VertexNormalColor>& visualMeshVertices)
{
	TIMER_INIT;
	TIMER_START;

    // 1. Create intermediate convex hull with limit count.
    std::vector<VMACH::ConvexHullVertex> vertices(visualMeshVertices.size());
    std::transform(visualMeshVertices.begin(), visualMeshVertices.end(), vertices.begin(), [](const VertexNormalColor& vertex) { return vertex.Position; });

    VMACH::ConvexHull ich(vertices, m_decompositionArgument.ICHIncludePointLimit);
    const std::list<VMACH::ConvexHullFace> ichFaceList = ich.GetFaces();

    // 2. Collect ICH face normals.
    std::vector<Vector3> ichFaceNormalVec;
    for (const VMACH::ConvexHullFace& f : ichFaceList)
    {
        Vector3 normal = (f.Vertices[1] - f.Vertices[0]).Cross(f.Vertices[2] - f.Vertices[0]);
        normal.Normalize();
        ichFaceNormalVec.push_back(normal);
    }

    // 3. Calculate min/max plane for k-DOP generation.
    std::vector<double> kdopMin(ichFaceNormalVec.size(), DBL_MAX);
    std::vector<double> kdopMax(ichFaceNormalVec.size(), -DBL_MAX);

    for (int v = 0; v < vertices.size(); v++)
    {
        for (int f = 0; f < ichFaceNormalVec.size(); f++)
        {
            double t = vertices[v].Dot(ichFaceNormalVec[f]);

            kdopMin[f] = std::min(kdopMin[f], t);
            kdopMax[f] = std::max(kdopMax[f], t);
        }
    }

	OutputDebugStringWFormat(L"ACH Time: ");
	TIMER_STOP_PRINT;

	// k-DOP.
	{
		// Add out of convex hull point for testing.
		vertices.emplace_back(-100, -100, -100);

		int insidePoint = 0;
		for (int v = 0; v < vertices.size(); v++)
		{
			for (int f = 0; f < ichFaceNormalVec.size(); f++)
			{
				float t = vertices[v].Dot(ichFaceNormalVec[f]) / ichFaceNormalVec[f].Length();

				if (kdopMin[f] <= t && t <= kdopMax[f])
				{
					insidePoint++;
					break;
				}
			}
		}

		OutputDebugStringWFormat(L"InsidePoint: %f\n", (float)insidePoint / vertices.size());
	}
}

void Surtr::TestECHCreation(_In_ const std::vector<VertexNormalColor>& visualMeshVertices)
{
	TIMER_INIT;
	TIMER_START;

	std::vector<VMACH::ConvexHullVertex> vertices(visualMeshVertices.size());
	std::transform(visualMeshVertices.begin(), visualMeshVertices.end(), vertices.begin(), [](const VertexNormalColor& vertex) { return vertex.Position; });

	VMACH::ConvexHull ch(vertices, 0);

    OutputDebugStringWFormat(L"ECH Time: ");
	TIMER_STOP_PRINT;

	double vol = 0;
	for (const auto& f : ch.GetFaces())
        vol += VMACH::ConvexHull::Volume(f, VMACH::ConvexHullVertex(0, 5, 0));

	OutputDebugStringWFormat(L"ECH Volume: %f\n", vol);
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

void Surtr::LoadModelData(
    _In_ const std::string fileName, 
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
}

Mesh* Surtr::PrepareMeshResource(_In_ const std::vector<VertexNormalColor>& vertices, _In_ const std::vector<uint32_t>& indices)
{
    Mesh* mesh = new Mesh();

    mesh->VertexData = vertices;
    mesh->IndexData = indices;
    mesh->VertexCount = vertices.size();
    mesh->IndexCount = indices.size();
	mesh->VBSize = sizeof(VertexNormalColor) * mesh->VertexCount;
	mesh->IBSize = sizeof(uint32_t) * mesh->IndexCount;

    // Prepare vertex buffer.
	{
		// Create default heap.
		CD3DX12_HEAP_PROPERTIES defaultHeapProp(D3D12_HEAP_TYPE_DEFAULT);
		auto resDesc = CD3DX12_RESOURCE_DESC::Buffer(mesh->VBSize);
		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommittedResource(
				&defaultHeapProp,
				D3D12_HEAP_FLAG_NONE,
				&resDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(mesh->VB.ReleaseAndGetAddressOf())));

		// Initialize vertex buffer view.
		mesh->VBV.BufferLocation = mesh->VB->GetGPUVirtualAddress();
        mesh->VBV.StrideInBytes = sizeof(VertexNormalColor);
        mesh->VBV.SizeInBytes = mesh->VBSize;

		// Create upload heap.
		CD3DX12_HEAP_PROPERTIES uploadHeapProp(D3D12_HEAP_TYPE_UPLOAD);
		auto uploadHeapDesc = CD3DX12_RESOURCE_DESC::Buffer(mesh->VBSize);
		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommittedResource(
				&uploadHeapProp,
				D3D12_HEAP_FLAG_NONE,
				&uploadHeapDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(mesh->VertexUploadHeap.ReleaseAndGetAddressOf())));

		// Define sub-resource data.
		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = vertices.data();
		subResourceData.RowPitch = mesh->VBSize;
		subResourceData.SlicePitch = mesh->VBSize;

		// Copy the vertex data to the default heap.
		UpdateSubresources(m_commandList.Get(), mesh->VB.Get(), mesh->VertexUploadHeap.Get(), 0, 0, 1, &subResourceData);

		// Translate vertex buffer state.
		const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			mesh->VB.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		m_commandList->ResourceBarrier(1, &barrier);
	}

    // Prepare index buffer.
	{
		// Create default heap.
		CD3DX12_HEAP_PROPERTIES defaultHeapProp(D3D12_HEAP_TYPE_DEFAULT);
		auto resDesc = CD3DX12_RESOURCE_DESC::Buffer(mesh->IBSize);
		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommittedResource(
				&defaultHeapProp,
				D3D12_HEAP_FLAG_NONE,
				&resDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(mesh->IB.ReleaseAndGetAddressOf())));

		// Initialize index buffer view.
		mesh->IBV.BufferLocation = mesh->IB->GetGPUVirtualAddress();
        mesh->IBV.Format = DXGI_FORMAT_R32_UINT;
        mesh->IBV.SizeInBytes = mesh->IBSize;

		// Create upload heap.
		CD3DX12_HEAP_PROPERTIES uploadHeapProp(D3D12_HEAP_TYPE_UPLOAD);
		auto uploadHeapDesc = CD3DX12_RESOURCE_DESC::Buffer(mesh->IBSize);
		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommittedResource(
				&uploadHeapProp,
				D3D12_HEAP_FLAG_NONE,
				&uploadHeapDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(mesh->IndexUploadHeap.ReleaseAndGetAddressOf())));

		// Define sub-resource data.
		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = indices.data();
		subResourceData.RowPitch = mesh->IBSize;
		subResourceData.SlicePitch = mesh->IBSize;

		// Copy the vertex data to the default heap.
		UpdateSubresources(m_commandList.Get(), mesh->IB.Get(), mesh->IndexUploadHeap.Get(), 0, 0, 1, &subResourceData);

		// Translate vertex buffer state.
		const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mesh->IB.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		m_commandList->ResourceBarrier(1, &barrier);
	}

    return mesh;
}

void Surtr::UpdateMeshData(Mesh* mesh, _In_ const std::vector<VertexNormalColor>& vertices, _In_ const std::vector<uint32_t>& indices)
{
	mesh->VertexCount = vertices.size();
	mesh->IndexCount = indices.size();
	mesh->VBSize = sizeof(VertexNormalColor) * mesh->VertexCount;
	mesh->IBSize = sizeof(uint32_t) * mesh->IndexCount;

    {
        mesh->VertexUploadHeap.Reset();

		// Create upload heap.
		CD3DX12_HEAP_PROPERTIES uploadHeapProp(D3D12_HEAP_TYPE_UPLOAD);
		auto uploadHeapDesc = CD3DX12_RESOURCE_DESC::Buffer(mesh->VBSize);
		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommittedResource(
				&uploadHeapProp,
				D3D12_HEAP_FLAG_NONE,
				&uploadHeapDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(mesh->VertexUploadHeap.ReleaseAndGetAddressOf())));

		// Define sub-resource data.
		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = vertices.data();
		subResourceData.RowPitch = mesh->VBSize;
		subResourceData.SlicePitch = mesh->VBSize;

		// Copy the vertex data to the default heap.
		UpdateSubresources(m_commandList.Get(), mesh->VB.Get(), mesh->VertexUploadHeap.Get(), 0, 0, 1, &subResourceData);

		// Translate vertex buffer state.
		const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			mesh->VB.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		m_commandList->ResourceBarrier(1, &barrier);
    }

    {
        mesh->IndexUploadHeap.Reset();

		// Create upload heap.
		CD3DX12_HEAP_PROPERTIES uploadHeapProp(D3D12_HEAP_TYPE_UPLOAD);
		auto uploadHeapDesc = CD3DX12_RESOURCE_DESC::Buffer(mesh->IBSize);
		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommittedResource(
				&uploadHeapProp,
				D3D12_HEAP_FLAG_NONE,
				&uploadHeapDesc,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(mesh->IndexUploadHeap.ReleaseAndGetAddressOf())));

		// Define sub-resource data.
		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = indices.data();
		subResourceData.RowPitch = mesh->IBSize;
		subResourceData.SlicePitch = mesh->IBSize;

		// Copy the vertex data to the default heap.
		UpdateSubresources(m_commandList.Get(), mesh->IB.Get(), mesh->IndexUploadHeap.Get(), 0, 0, 1, &subResourceData);

		// Translate vertex buffer state.
		const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			mesh->IB.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
		m_commandList->ResourceBarrier(1, &barrier);
    }
}
