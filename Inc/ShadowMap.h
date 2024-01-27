#pragma once

class ShadowMap
{
public:
	ShadowMap(ID3D12Device* device,
		UINT width, UINT height);

	ShadowMap(const ShadowMap& rhs) = delete;
	ShadowMap& operator=(const ShadowMap& rhs) = delete;
	~ShadowMap() = default;

	UINT Width()const;
	UINT Height()const;
	ID3D12Resource* Resource();
	CD3DX12_GPU_DESCRIPTOR_HANDLE Srv()const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE Dsv()const;

	D3D12_VIEWPORT Viewport()const;
	D3D12_RECT ScissorRect()const;

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDsv);

	void OnResize(UINT newWidth, UINT newHeight);

private:
	void BuildDescriptors();
	void BuildResource();

private:
	ID3D12Device* m_device = nullptr;

	D3D12_VIEWPORT m_viewport;
	D3D12_RECT m_scissorRect;

	UINT m_width = 0;
	UINT m_height = 0;
	DXGI_FORMAT m_format = DXGI_FORMAT_R32_TYPELESS;

	CD3DX12_CPU_DESCRIPTOR_HANDLE m_srvHandleCPU;
	CD3DX12_GPU_DESCRIPTOR_HANDLE m_srvHandleGPU;
	CD3DX12_CPU_DESCRIPTOR_HANDLE m_dsvHandleCPU;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_shadowMapResource = nullptr;
};

