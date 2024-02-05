#include "pch.h"
#include "Surtr.h"

#include "voro++.hh"
#include "DT.h"
#include "DT3D.h"

extern void ExitGame() noexcept;

using namespace DirectX;
using namespace SimpleMath;
using Microsoft::WRL::ComPtr;

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

// Initialize the Direct3D resources required to run.
void Surtr::InitializeD3DResources(HWND window, int width, int height, UINT subDivideCount, UINT shadowMapSize, BOOL fullScreenMode)
{
    m_window = window;
    m_outputWidth = std::max(width, 1);
    m_outputHeight = std::max(height, 1);
    m_aspectRatio = static_cast<float>(m_outputWidth) / static_cast<float>(m_outputHeight);
    m_fullScreenMode = fullScreenMode;

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
    m_camPosition = XMVectorSet(0.0f, 5.0f, 10.0f, 0.0f);
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

        CreateACH(m_meshVec[0]->VertexData, m_meshVec[0]->IndexData, achVertexData, achIndexData);
        
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

            cbShadow.lightWorldMatrix = XMMatrixTranspose(lightWorld);
            cbShadow.lightViewProjMatrix = XMMatrixTranspose(lightView * lightProj);
            cbShadow.cameraPosition = m_camPosition;

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
				if (Mesh::RenderOptionType::SOLID & m_meshVec[i]->RenderOption)
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

            cbOpaque.worldMatrix = XMMatrixTranspose(m_worldMatrix);
            cbOpaque.viewProjMatrix = XMMatrixTranspose(m_viewMatrix * m_projectionMatrix);
            XMStoreFloat4(&cbOpaque.cameraPosition, m_camPosition);
            XMStoreFloat4(&cbOpaque.lightDirection, m_lightDirection);
            cbOpaque.lightColor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

            cbOpaque.shadowTransform = XMMatrixTranspose(XMLoadFloat4x4(&m_shadowTransform));

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
                    ImGui::SliderInt("ICH Included Points", &m_decompositionArgument.ichIncludePointLimit, 20, 1000);
                    ImGui::SliderFloat("ACH Plane Gap Inverse", &m_decompositionArgument.achPlaneGapInverse, 0.0f, 10000.0f);
					if (ImGui::Button("Regenerate!")) { m_executeNextStep = true; }

                    ImGui::Dummy(ImVec2(0.0f, 10.0f));

                    ImGui::Text("[Results]");
                    ImGui::Text("ICH Face Count: %d", m_decompositionResult.ichFaceCnt);
                    
                    if (m_decompositionResult.achErrorPointCnt == 0)
                        ImGui::TextColored(ImVec4(0, 1, 0, 1), "ALL VERTEX CONTAINED");
                    else
                        ImGui::TextColored(ImVec4(1, 0, 0, 1), "%d VERTEX NOT CONTAINED!", m_decompositionResult.achErrorPointCnt);

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

#if RENDER_OBJECT == 0
    LoadModelData("Resources\\Models\\lucy.obj", XMFLOAT3(50, 50, 50), XMFLOAT3(0, 20, 0), objectVertexData, objectIndexData);
#elif RENDER_OBJECT == 1
    LoadModelData("Resources\\Models\\stanford-bunny.obj", XMFLOAT3(70, 70, 70), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
#else
    LoadModelData("Resources\\Models\\lowpoly-bunny.obj", XMFLOAT3(70, 70, 70), XMFLOAT3(0, 0, 0), objectVertexData, objectIndexData);
#endif
    
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
	CreateACH(objectVertexData, objectIndexData, convexHullVertexData, convexHullIndexData);

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

void loop(std::vector<VMACH::PolygonFace>& faceVec, VMACH::PolygonFace res, const std::vector<DT3D::Edge>& voronoiEdgeVec, const Vector3& findPoint)
{
    int counter = 0;
	for (int i = 0; i < voronoiEdgeVec.size(); i++)
	{
		if (voronoiEdgeVec[i].p0 == findPoint || voronoiEdgeVec[i].p1 == findPoint)
		{
            Vector3 nextPoint = voronoiEdgeVec[i].p0 == findPoint ? voronoiEdgeVec[i].p1 : voronoiEdgeVec[i].p0;
            if (res.VertexVec.size() >= 3 && std::abs(res.CalcDistanceToPoint(nextPoint)) > 0.0001)
                continue;

            if (res.VertexVec.size() >= 3 && nextPoint == res.VertexVec[0])
            {
                faceVec.push_back(res);
                continue;
            }

            bool dup = res.VertexVec.end() != std::find(res.VertexVec.begin(), res.VertexVec.end(), nextPoint);
            if (FALSE == dup)
            {
				res.AddVertex(nextPoint);
				loop(faceVec, res, voronoiEdgeVec, nextPoint);
            }
		}
	}

    OutputDebugStringWFormat(L"%d\n", counter);
}

void Surtr::CreateACH(
    _In_ const std::vector<VertexNormalColor>& visualMeshVertices, 
    _In_ const std::vector<uint32_t>& visualMeshIndices, 
    _Out_ std::vector<VertexNormalColor>& achVertexData, 
    _Out_ std::vector<uint32_t>& achIndexData)
{
    achVertexData = std::vector<VertexNormalColor>();
    achIndexData = std::vector<uint32_t>();

    // Test 3D Voronoi Generation.
    const auto voronoiGen = [&]() 
    {
        std::vector<Vector3> points;
        int pCount = 0;
        while (pCount < 50)
        {
            float x = rnd() * 5;
            float y = rnd() * 5;
            float z = rnd() * 5;

            Vector3 p(x, y, z);

            if (points.end() == std::find_if(points.begin(), points.end(), [&](const Vector3& pp) { return (p - pp).Length() < 1; }))
            {
                points.emplace_back(x, y, z);
                pCount++;
            }
        }

        const auto dt = DT3D::Triangulate(points);
        // Render triangulate.
        /*for (int i = 0; i < dt.faces.size(); i++)
        {
            achVertexData.push_back(VertexNormalColor(dt.faces[i].p0));
            achVertexData.push_back(VertexNormalColor(dt.faces[i].p1));
            achVertexData.push_back(VertexNormalColor(dt.faces[i].p2));

            achIndexData.push_back(achIndexData.size());
            achIndexData.push_back(achIndexData.size());
            achIndexData.push_back(achIndexData.size());
        }*/

        const std::vector<DT3D::Edge> voronoiEdgeVec = DT3D::Voronoi(dt);

        std::vector<VMACH::PolygonFace> voronoiFaceVec;
        std::vector<VMACH::PolygonFace> uniqueVoronoiFaceVec;
        {
            for (int i = 0; i < voronoiEdgeVec.size(); i++)
            {
                const Vector3 startPoint = voronoiEdgeVec[i].p0;

                std::vector<VMACH::PolygonFace> faceVec;
                VMACH::PolygonFace res;
                res.AddVertex(startPoint);
                loop(faceVec, res, voronoiEdgeVec, startPoint);

                voronoiFaceVec.insert(voronoiFaceVec.end(), faceVec.begin(), faceVec.end());
            }

            for (int i = 0; i < voronoiEdgeVec.size(); i++)
            {
                const Vector3 startPoint = voronoiEdgeVec[i].p1;

                std::vector<VMACH::PolygonFace> faceVec;
                VMACH::PolygonFace res;
                res.AddVertex(startPoint);
                loop(faceVec, res, voronoiEdgeVec, startPoint);

                voronoiFaceVec.insert(voronoiFaceVec.end(), faceVec.begin(), faceVec.end());
            }
        }

        Unique<VMACH::PolygonFace>(voronoiFaceVec, uniqueVoronoiFaceVec);

        // Render voronoi faces.
        for (int i = 0; i < uniqueVoronoiFaceVec.size(); i++)
        {
            double a, b, c;
            a = rnd(); b = rnd(); c = rnd();
            XMFLOAT3 color(a, b, c);

            uniqueVoronoiFaceVec[i].Reorder();
            uniqueVoronoiFaceVec[i].Render(achVertexData, achIndexData, color);
        }

        // Render circumsphere center.
        for (int i = 0; i < dt.TetVec.size(); i++)
        {
            Vector3 v0(dt.TetVec[i].sphere.center);
            Vector3 v1 = v0 + Vector3(0.01f, 0, 0);
            Vector3 v2 = v1 + Vector3(0, 0, 0.01f);

            achVertexData.push_back(VertexNormalColor(v0));
            achVertexData.push_back(VertexNormalColor(v1));
            achVertexData.push_back(VertexNormalColor(v2));

            achIndexData.push_back(achIndexData.size());
            achIndexData.push_back(achIndexData.size());
            achIndexData.push_back(achIndexData.size());
        }

        // Render voronoi edges.
        /*for (int i = 0; i < voronoiEdgeVec.size(); i++)
        {
            Vector3 v0(voronoiEdgeVec[i].p0);
            Vector3 v1(voronoiEdgeVec[i].p1);
            Vector3 v2(voronoiEdgeVec[i].p1.x + 0.001f, voronoiEdgeVec[i].p1.y, voronoiEdgeVec[i].p1.z);

            achVertexData.push_back(VertexNormalColor(v0));
            achVertexData.push_back(VertexNormalColor(v1));
            achVertexData.push_back(VertexNormalColor(v2));

            achIndexData.push_back(achIndexData.size());
            achIndexData.push_back(achIndexData.size());
            achIndexData.push_back(achIndexData.size());
        }*/
    };

    // 1. Create intermediate convex hull with limit count.
    std::vector<VMACH::ConvexHullVertex> vertices(visualMeshVertices.size());
    std::transform(visualMeshVertices.begin(), visualMeshVertices.end(), vertices.begin(), [](const VertexNormalColor& vertex) { return vertex.position; });

    VMACH::ConvexHull ich(vertices, m_decompositionArgument.ichIncludePointLimit);
    const std::list<VMACH::ConvexHullFace> ichFaceList = ich.GetFaces();
    // ich.Render(achVertexData, achIndexData);

    // 2. Collect ICH face normals.
    std::vector<Vector3> ichFaceNormalVec;
    for (const VMACH::ConvexHullFace& f : ichFaceList)
    {
        Vector3 normal = (f.Vertices[1] - f.Vertices[0]).Cross(f.Vertices[2] - f.Vertices[0]);
        normal.Normalize();
        ichFaceNormalVec.push_back(normal);
    }

    m_decompositionResult.ichFaceCnt = ichFaceNormalVec.size();

    // 3. Calculate bounding box.
    double minX, maxX, minY, maxY, minZ, maxZ;
    {
		const auto x = std::minmax_element(vertices.begin(), vertices.end(),
			[](const VMACH::ConvexHullVertex& p1, const VMACH::ConvexHullVertex& p2) { return p1.x < p2.x; });
		const auto y = std::minmax_element(vertices.begin(), vertices.end(),
			[](const VMACH::ConvexHullVertex& p1, const VMACH::ConvexHullVertex& p2) { return p1.y < p2.y; });
		const auto z = std::minmax_element(vertices.begin(), vertices.end(),
			[](const VMACH::ConvexHullVertex& p1, const VMACH::ConvexHullVertex& p2) { return p1.z < p2.z; });

		minX = (*x.first).x;    maxX = (*x.second).x;
        minY = (*y.first).y;    maxY = (*y.second).y;
        minZ = (*z.first).z;    maxZ = (*z.second).z;
    }

	Vector3 bbCenter((maxX + minX) / 2.0, (maxY + minY) / 2.0, (maxZ + minZ) / 2.0);
    double maxAxisScale = std::max(std::max(maxX - minX, maxY - minY), maxZ - minZ);

    // 4. Calculate min/max plane for k-DOP generation.
	std::vector<double> kdopMin(ichFaceNormalVec.size(), DBL_MAX);
	std::vector<double> kdopMax(ichFaceNormalVec.size(), -DBL_MAX);
    
    std::vector<Plane> kdopMinPlane(ichFaceNormalVec.size());
    std::vector<Plane> kdopMaxPlane(ichFaceNormalVec.size());

	std::vector<Vector3> kdopMinVertex(ichFaceNormalVec.size());
	std::vector<Vector3> kdopMaxVertex(ichFaceNormalVec.size());

    for (int v = 0; v < vertices.size(); v++)
    {
        for (int f = 0; f < ichFaceNormalVec.size(); f++)
        {
            float t = vertices[v].Dot(ichFaceNormalVec[f]);

            if (kdopMin[f] > t)
            {
                kdopMin[f] = t;
                kdopMinPlane[f] = Plane(vertices[v], -ichFaceNormalVec[f]);
                kdopMinVertex[f] = vertices[v];
            }

            if (kdopMax[f] < t)
            {
                kdopMax[f] = t;
                kdopMaxPlane[f] = Plane(vertices[v], ichFaceNormalVec[f]);
                kdopMaxVertex[f] = vertices[v];
            }
        }
    }

    // 5. Init bounding box polygon.
    VMACH::Polygon3D bbPolygon = VMACH::GetBoxPolygon();
    bbPolygon.Scale(Vector3((maxX - minX), (maxY - minY), (maxZ - minZ)));
    bbPolygon.Scale(2.0);
    bbPolygon.Translate(bbCenter);

    // 6. Collect polygon face using ICH face planes.
    const auto collectPolygonFaces = [&](Plane p, Vector3 x)
    {
		VMACH::PolygonFace cf;

		Vector3 n = p.Normal();
		n.Normalize();

        Vector3 tmp(1, 2, 3);
        Vector3 u = n.Cross(tmp);
		u.Normalize();

        Vector3 v = u.Cross(n);
		v.Normalize();

        // #CORRECTION
        Vector3 p1 = x + u * 100000 - v * 100000 + n * maxAxisScale / m_decompositionArgument.achPlaneGapInverse;
        Vector3 p2 = x + u * 100000 + v * 100000 + n * maxAxisScale / m_decompositionArgument.achPlaneGapInverse;
        Vector3 p3 = x - u * 100000 + v * 100000 + n * maxAxisScale / m_decompositionArgument.achPlaneGapInverse;
        Vector3 p4 = x - u * 100000 - v * 100000 + n * maxAxisScale / m_decompositionArgument.achPlaneGapInverse;

		cf.AddVertex(p1); cf.AddVertex(p2); cf.AddVertex(p3); cf.AddVertex(p4);
        cf.Rewind();
        return cf;
    };

    VMACH::Polygon3D clippingPolygon;
    for (int f = 0; f < ichFaceNormalVec.size(); f++)
    {
        clippingPolygon.FaceVec.push_back(collectPolygonFaces(kdopMaxPlane[f], kdopMaxVertex[f]));
        clippingPolygon.FaceVec.push_back(collectPolygonFaces(kdopMinPlane[f], kdopMinVertex[f]));
    }

    // 7. Clip bounding box polygon with clipping faces.
	VMACH::Polygon3D achPoly = VMACH::Polygon3D::ClipPolygon(bbPolygon, clippingPolygon);

	// Check ACH contains all points or not.
    m_decompositionResult.achErrorPointCnt = 0;
	for (int i = 0; i < visualMeshVertices.size(); i++)
	{
		Vector3 vertPos = visualMeshVertices[i].position;

		VMACH::Polygon3D boxPoly = VMACH::GetBoxPolygon();
		boxPoly.Scale(0.01);
		boxPoly.Translate(vertPos);

        bool isContain = achPoly.Contains(vertPos);
        if (FALSE == isContain)
            m_decompositionResult.achErrorPointCnt++;

		boxPoly.Render(achVertexData, achIndexData, isContain ? Vector3(0, 1, 0) : Vector3(1, 0, 0));
	}

	// Generate mesh polygon.
	VMACH::Polygon3D meshPolygon;
	for (uint32_t i = 0; i < visualMeshIndices.size(); i += 3)
	{
		VMACH::PolygonFace face;
		face.AddVertex(visualMeshVertices[visualMeshIndices[i]].position);
		face.AddVertex(visualMeshVertices[visualMeshIndices[i + 1]].position);
		face.AddVertex(visualMeshVertices[visualMeshIndices[i + 2]].position);

		meshPolygon.FaceVec.push_back(face);
	}

    const auto clippedMesh = VMACH::Polygon3D::ClipMeshFace(
        meshPolygon, 
        VMACH::PolygonFace({ Vector3(100, 3, -100), Vector3(-100, 3, -100), Vector3(-100, 3, 100), Vector3(100, 3, 100) }));
    
    clippedMesh.Render(achVertexData, achIndexData);

    return;

    // 8. Voronoi diagram generation.
    Vector3 voroBBMinVec(minX, minY, minZ);
    Vector3 voroBBMaxVec(maxX, maxY, maxZ);
    const int cellCount = 48;

	voro::container voroCon(
        voroBBMinVec.x, voroBBMaxVec.x, 
        voroBBMinVec.y, voroBBMaxVec.y, 
        voroBBMinVec.z, voroBBMaxVec.z, 2, 2, 2, false, false, false, 8);

	int i = 0;
	double x, y, z;
	while (i < cellCount)
	{
		x = voroBBMinVec.x + rnd() * (voroBBMaxVec.x - voroBBMinVec.x) * 0.75f;
		y = voroBBMinVec.y + rnd() * (voroBBMaxVec.y - voroBBMinVec.y) * 0.75f;
		z = voroBBMinVec.z + rnd() * (voroBBMaxVec.z - voroBBMinVec.z) * 0.75f;
		if (voroCon.point_inside(x, y, z))
		{
			voroCon.put(i, x, y, z);
			i++;
		}
	}

    std::vector<VMACH::Polygon3D> voroPolyVec;

	int id;
	voro::voronoicell_neighbor voroCell;
	std::vector<int> neighborVec;

	voro::c_loop_all cl(voroCon);
	int dimension = 0;
	if (cl.start()) do if (voroCon.compute_cell(voroCell, cl)) 
    {
		dimension += 1;
	} while (cl.inc());

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

        VMACH::Polygon3D voroPoly;

        int cur = 0;
        while (cur < cellFaceVec.size())
        {
            VMACH::PolygonFace face;
            
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
            voroPoly.FaceVec.push_back(face);
        }

        voroPolyVec.push_back(voroPoly);

		counter += 1;
	} while (cl.inc());

	// Render voro poly.
	/*for (int i = 0; i < voroPolyVec.size(); i++)
	{
		double a, b, c;
		a = rnd(); b = rnd(); c = rnd();
		XMFLOAT3 color(a, b, c);

		Vector3 outer = voroPolyVec[i].GetCentroid() - bbCenter;
		outer.Normalize();
		outer *= 2;

		voroPolyVec[i].Translate(outer);
		voroPolyVec[i].Render(achVertexData, achIndexData, color);
	}*/

    // Render clipped convex.
    for (int i = 0; i < voroPolyVec.size(); i++)
    {
		double a, b, c;
		a = rnd(); b = rnd(); c = rnd();
		XMFLOAT3 color(a, b, c);

        VMACH::Polygon3D clippedPoly = VMACH::Polygon3D::ClipPolygon(achPoly, voroPolyVec[i]);
        VMACH::Polygon3D clippedMeshPoly = VMACH::Polygon3D::ClipMesh(meshPolygon, voroPolyVec[i]);

		Vector3 outer = voroPolyVec[i].GetCentroid() - bbCenter;
		outer.Normalize();
		outer *= 2;

        // clippedPoly.Translate(outer);
        // clippedPoly.Render(achVertexData, achIndexData, color);
        
        clippedMeshPoly.Translate(outer);
        clippedMeshPoly.Render(achVertexData, achIndexData, color);
    }
}

void Surtr::TestACHCreation(_In_ const std::vector<VertexNormalColor>& visualMeshVertices)
{
	TIMER_INIT;
	TIMER_START;

    // 1. Create intermediate convex hull with limit count.
    std::vector<VMACH::ConvexHullVertex> vertices(visualMeshVertices.size());
    std::transform(visualMeshVertices.begin(), visualMeshVertices.end(), vertices.begin(), [](const VertexNormalColor& vertex) { return vertex.position; });

    VMACH::ConvexHull ich(vertices, m_decompositionArgument.ichIncludePointLimit);
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

	// #TEST k-DOP.
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
	std::transform(visualMeshVertices.begin(), visualMeshVertices.end(), vertices.begin(), [](const VertexNormalColor& vertex) { return vertex.position; });

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
		aiProcess_FlipWindingOrder);

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
