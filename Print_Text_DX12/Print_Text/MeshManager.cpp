//======================================================================================
//	Ed Kurlyak 2023 Render To Texture DirectX12
//======================================================================================

#include "MeshManager.h"

CMeshManager::CMeshManager()
{
}

CMeshManager::~CMeshManager()
{
	if (m_d3dDevice != nullptr)
		FlushCommandQueue();
}

void CMeshManager::EnableDebugLayer_CreateFactory()
{
#if defined(DEBUG) || defined(_DEBUG) 
	{
		Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
		debugController->EnableDebugLayer();
	}
#endif

	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory)));
}

void CMeshManager::Create_Device()
{
	std::vector <IDXGIAdapter*> vAdapters;
	IDXGIAdapter* pAdapter;
	
	for (UINT i = 0;
		m_dxgiFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND;
		++i)
	{
		vAdapters.push_back(pAdapter);
	}

	DXGI_ADAPTER_DESC AdapterDesc;
	vAdapters[0]->GetDesc(&AdapterDesc);

	StrAdapterName = AdapterDesc.Description;

	HRESULT hardwareResult = D3D12CreateDevice(
		nullptr,             // default adapter
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&m_d3dDevice));

	// Fallback to WARP device.
	if (FAILED(hardwareResult))
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter> pWarpAdapter;
		ThrowIfFailed(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			pWarpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_d3dDevice)));
	}
}

void CMeshManager::CreateFence_GetDescriptorsSize()
{
	ThrowIfFailed(m_d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&m_Fence)));

	m_RtvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_DsvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_CbvSrvUavDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

}

void CMeshManager::Check_Multisample_Quality()
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = m_BackBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;
	ThrowIfFailed(m_d3dDevice->CheckFeatureSupport(
		D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
		&msQualityLevels,
		sizeof(msQualityLevels)));

	m_4xMsaaQuality = msQualityLevels.NumQualityLevels;
	assert(m_4xMsaaQuality > 0 && "Unexpected MSAA quality level.");
}

void CMeshManager::Create_CommandList_Allocator_Queue()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue)));

	ThrowIfFailed(m_d3dDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(m_DirectCmdListAlloc.GetAddressOf())));

	ThrowIfFailed(m_d3dDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_DirectCmdListAlloc.Get(),
		nullptr,
		IID_PPV_ARGS(m_CommandList.GetAddressOf())));

	m_CommandList->Close();
}

void CMeshManager::Create_SwapChain()
{
	m_SwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = m_ClientWidth;
	sd.BufferDesc.Height = m_ClientHeight;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = m_BackBufferFormat;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = m_4xMsaaState ? 4 : 1;
	sd.SampleDesc.Quality = m_4xMsaaState ? (m_4xMsaaQuality - 1) : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = m_SwapChainBufferCount;
	sd.OutputWindow = m_hWnd;
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	ThrowIfFailed(m_dxgiFactory->CreateSwapChain(
		m_CommandQueue.Get(),
		&sd,
		m_SwapChain.GetAddressOf()));
}

void CMeshManager::Resize_SwapChainBuffers()
{
	assert(m_d3dDevice);
	assert(m_SwapChain);
	assert(m_DirectCmdListAlloc);

	FlushCommandQueue();

	ThrowIfFailed(m_CommandList->Reset(m_DirectCmdListAlloc.Get(), nullptr));

	for (int i = 0; i < m_SwapChainBufferCount; ++i)
		m_SwapChainBuffer[i].Reset();

	m_DepthStencilBuffer.Reset();

	ThrowIfFailed(m_SwapChain->ResizeBuffers(
		m_SwapChainBufferCount,
		m_ClientWidth, m_ClientHeight,
		m_BackBufferFormat,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));
}

void CMeshManager::FlushCommandQueue()
{
	m_CurrentFence++;

	ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence));

	if (m_Fence->GetCompletedValue() < m_CurrentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

		ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrentFence, eventHandle));

		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

void CMeshManager::Create_Dsv_DescriptorHeaps_And_View()
{
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(m_DsvHeap.GetAddressOf())));

	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = m_ClientWidth;
	depthStencilDesc.Height = m_ClientHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = m_DepthStencilTextureFormat;
	depthStencilDesc.SampleDesc.Count = m_4xMsaaState ? 4 : 1;
	depthStencilDesc.SampleDesc.Quality = m_4xMsaaState ? (m_4xMsaaQuality - 1) : 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = m_DepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;
	ThrowIfFailed(m_d3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(m_DepthStencilBuffer.GetAddressOf())));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = m_DepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	m_d3dDevice->CreateDepthStencilView(m_DepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_DepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));
}

D3D12_CPU_DESCRIPTOR_HANDLE CMeshManager::DepthStencilView()
{
	return m_DsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void CMeshManager::Execute_Init_Commands()
{
	ThrowIfFailed(m_CommandList->Close());
	ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
	m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();
}

void CMeshManager::Update_ViewPort_And_Scissor()
{
	m_ScreenViewport.TopLeftX = 0;
	m_ScreenViewport.TopLeftY = 0;
	m_ScreenViewport.Width = static_cast<float>(m_ClientWidth);
	m_ScreenViewport.Height = static_cast<float>(m_ClientHeight);
	m_ScreenViewport.MinDepth = 0.0f;
	m_ScreenViewport.MaxDepth = 1.0f;

	m_ScissorRect = { 0, 0, m_ClientWidth, m_ClientHeight };
}

void CMeshManager::LoadTextures()
{
	ThrowIfFailed(m_CommandList->Reset(m_DirectCmdListAlloc.Get(), nullptr));

	auto FontTex = std::make_unique<Texture>();
	FontTex->Name = "FontTex";
	FontTex->Filename = L"./ExportedFont.bmp";

	//открываем BMP файл для чтения в бинарном режиме
	FILE* Fp = NULL;
	fopen_s(&Fp, "ExportedFont.bmp", "rb");
	if (Fp == NULL)
		MessageBox(NULL, L"Error Open File", L"INFO", MB_OK);

	//читаем заголовок файла текстуры
	BITMAPFILEHEADER Bfh;
	fread(&Bfh, sizeof(BITMAPFILEHEADER), 1, Fp);

	//читаем заголовок файла текстуры
	BITMAPINFOHEADER Bih;
	fread(&Bih, sizeof(BITMAPINFOHEADER), 1, Fp);

	//сдвигаемся от начала BMP файла до начала данных
	fseek(Fp, Bfh.bfOffBits, SEEK_SET);

	//указатель на массив байт полученных из BMP файла
	unsigned char* ResTemp;
	ResTemp = new unsigned char[Bih.biHeight * Bih.biWidth * 4];
	//читаем из файла rgb данные изображения
	fread(ResTemp, Bih.biHeight * Bih.biWidth * 4, 1, Fp);

	//загрузили текстуру закрываем файл
	fclose(Fp);

	TextureWidth = Bih.biWidth;
	TextureHeight = Bih.biHeight;

	unsigned char* Res = new unsigned char[TextureWidth * TextureHeight * 4];

	int index = 0;
	for (int h = 0; h < Bih.biHeight; h++)
	{
		for (int w = 0; w < Bih.biWidth; w++)
		{

			//переворачиваем BMP файл по вертикали
			int Index1 = (h * Bih.biWidth + w) * 4;
			int Index2 = ((Bih.biHeight - 1 - h) * Bih.biWidth + w) * 4;
			
			//меняем RGB местами
			Res[Index2 + 0] = ResTemp[Index1 + 0];
			Res[Index2 + 1] = ResTemp[Index1 + 1];
			Res[Index2 + 2] = ResTemp[Index1 + 2];
			Res[Index2 + 3] = ResTemp[Index1 + 3];
		}
	}

	FontTex->Resource = CreateTexture(m_d3dDevice.Get(),
		m_CommandList.Get(), Res, TextureWidth * TextureHeight * 4, FontTex->UploadHeap);
	
	delete[] Res;

	m_Textures[FontTex->Name] = std::move(FontTex);
}

Microsoft::WRL::ComPtr<ID3D12Resource> CMeshManager::CreateTexture(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
	const void* initData,
	UINT64 byteSize,
	Microsoft::WRL::ComPtr<ID3D12Resource>& UploadBuffer)
{
	Microsoft::WRL::ComPtr<ID3D12Resource> m_Texture;
	
	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.Width = TextureWidth;
	textureDesc.Height = TextureHeight;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(m_Texture.GetAddressOf())));

	const UINT64 UploadBufferSize = GetRequiredIntermediateSize(m_Texture.Get(), 0, 1);

	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(UploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&UploadBuffer)));

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = initData;
	textureData.RowPitch = TextureWidth * 4;
	textureData.SlicePitch = textureData.RowPitch * TextureHeight;

	UpdateSubresources(cmdList, m_Texture.Get(), UploadBuffer.Get(), 0, 0, 1, &textureData);

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_Texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

	return m_Texture;
}

void CMeshManager::Create_ShaderResource_Heap_And_View_Pass1()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SrvDescriptorHeap)));

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto WoodCrateTex = m_Textures["FontTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = WoodCrateTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = WoodCrateTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	m_d3dDevice->CreateShaderResourceView(WoodCrateTex.Get(), &srvDesc, hDescriptor);

}

void CMeshManager::Create_Main_RenderTargetHeap_And_View_Pass1()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = m_SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(m_RtvHeap.GetAddressOf())));

	m_CurrBackBuffer = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(m_RtvHeap->GetCPUDescriptorHandleForHeapStart());

	for (UINT i = 0; i < m_SwapChainBufferCount; i++)
	{
		ThrowIfFailed(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_SwapChainBuffer[i])));
		m_d3dDevice->CreateRenderTargetView(m_SwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, m_RtvDescriptorSize);
	}
}

void CMeshManager::Create_Frame_Resources()
{
	for (int i = 0; i < m_NumFrameResources; ++i)
	{
		m_FrameResources.push_back(std::make_unique<FrameResource>(m_d3dDevice.Get(),
			1, 0));
	}
}


void CMeshManager::Create_Cube_Shaders_And_InputLayout_Pass1()
{
	m_VsByteCode = d3dUtil::CompileShader(L"Shaders\\tex.hlsl", nullptr, "VS", "vs_5_0");
	m_PsByteCode = d3dUtil::CompileShader(L"Shaders\\tex.hlsl", nullptr, "PS", "ps_5_0");

	m_InputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void CMeshManager::Create_RootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	CD3DX12_DESCRIPTOR_RANGE cbvTable;
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable, D3D12_SHADER_VISIBILITY_ALL);
	slotRootParameter[1].InitAsConstantBufferView(0);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> SerializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> ErrorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		SerializedRootSig.GetAddressOf(), ErrorBlob.GetAddressOf());

	if (ErrorBlob != nullptr)
	{
		::OutputDebugStringA((char*)ErrorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(m_d3dDevice->CreateRootSignature(
		0,
		SerializedRootSig->GetBufferPointer(),
		SerializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&m_RootSignature)));
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> CMeshManager::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC PointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC PointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC LinearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC LinearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC AnisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC AnisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		PointWrap, PointClamp,
		LinearWrap, LinearClamp,
		AnisotropicWrap, AnisotropicClamp };
}

void CMeshManager::Create_PipelineStateObject_Pass1()
{

	CD3DX12_BLEND_DESC Blend = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	Blend.RenderTarget[0].BlendEnable = true;
	//Blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	//Blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	Blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	Blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	Blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	Blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
	Blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	Blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	Blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { m_InputLayout.data(), (UINT)m_InputLayout.size() };
	psoDesc.pRootSignature = m_RootSignature.Get();
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(m_VsByteCode->GetBufferPointer()),
		m_VsByteCode->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(m_PsByteCode->GetBufferPointer()),
		m_PsByteCode->GetBufferSize()
	};
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	//psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = Blend;
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = m_BackBufferFormat;
	psoDesc.SampleDesc.Count = m_4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = m_4xMsaaState ? (m_4xMsaaQuality - 1) : 0;
	psoDesc.DSVFormat = m_DepthStencilFormat;
	ThrowIfFailed(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PSO)));
}

D3D12_CPU_DESCRIPTOR_HANDLE CMeshManager::CurrentBackBufferView()
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		m_RtvHeap->GetCPUDescriptorHandleForHeapStart(),
		m_CurrBackBuffer,
		m_RtvDescriptorSize);
}

ID3D12Resource* CMeshManager::CurrentBackBuffer()
{
	return m_SwapChainBuffer[m_CurrBackBuffer].Get();
}

void CMeshManager::Init_MeshManager(HWND hWnd)
{
	m_hWnd = hWnd;

	EnableDebugLayer_CreateFactory();

	Create_Device();

	CreateFence_GetDescriptorsSize();

	Check_Multisample_Quality();

	Create_CommandList_Allocator_Queue();

	Create_SwapChain();

	Resize_SwapChainBuffers();

	Create_Dsv_DescriptorHeaps_And_View();

	Execute_Init_Commands();

	Update_ViewPort_And_Scissor();

	LoadTextures();

	Create_ShaderResource_Heap_And_View_Pass1();

	Create_Main_RenderTargetHeap_And_View_Pass1();

	Create_Frame_Resources();

	Create_Cube_Shaders_And_InputLayout_Pass1();

	Create_RootSignature();

	Create_PipelineStateObject_Pass1();

	Execute_Init_Commands();

	DirectX::XMVECTOR Pos = DirectX::XMVectorSet(0, 0.0f, -8.0f, 1.0f);
	DirectX::XMVECTOR Target = DirectX::XMVectorZero();
	DirectX::XMVECTOR Up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	DirectX::XMMATRIX MatView = DirectX::XMMatrixLookAtLH(Pos, Target, Up);
	DirectX::XMStoreFloat4x4(&m_View, MatView);

	DirectX::XMMATRIX MatProj = DirectX::XMMatrixPerspectiveFovLH(0.25f * DirectX::XM_PI, 4.0f / 3.0f, 1.0f, 1000.0f);
	XMStoreFloat4x4(&m_Proj, MatProj);
	
	m_Timer.Timer_Start(30);
}

void CMeshManager::Print_Text(unsigned int StringNum, char* Text, float x, float y, float SizeX, float SizeY)
{

	UINT Length = (UINT)strlen(Text);

	std::vector<Vertex> VertBuff;

	for (unsigned int i = 0; i < Length; i++)
	{
		//ширина клетки 16 пикселей
		//ширина изображения текстуры со шрифтом
		float TexWidth = 256.0f;
		//отступ перед буквой в клетке 3 пикселя
		float Tw1 = 3.0f / TexWidth;
		//ширина буквы 9 пикселей
		float Tw2 = 9.0f / TexWidth;

		char character = Text[i];
		float uv_x = (character % 16) / 16.0f;
		float uv_y = (character / 16) / 16.0f;

		Vertex VertexUpLeft = { x + i * SizeX,			y,	0.0f, uv_x + Tw1,		uv_y };
		Vertex VertexUpRight = { x + i * SizeX + SizeX,	y,	0.0f, uv_x + Tw1 + Tw2,	uv_y };
		Vertex VertexDownRight = { x + i * SizeX + SizeX,	y + SizeY,	0.0f, uv_x + Tw1 + Tw2,	uv_y + 1.0f / 16.0f };
		Vertex VertexDownLeft = { x + i * SizeX,			y + SizeY,	0.0f, uv_x + Tw1,		uv_y + 1.0f / 16.0f };

		VertBuff.push_back(VertexDownLeft);
		VertBuff.push_back(VertexUpLeft);
		VertBuff.push_back(VertexUpRight);

		VertBuff.push_back(VertexDownLeft);
		VertBuff.push_back(VertexUpRight);
		VertBuff.push_back(VertexDownRight);
	}

	Vert_Buff_DX[StringNum] = std::make_unique<UploadBuffer<Vertex>>(m_d3dDevice.Get(), VertBuff.size(), false);

	for (unsigned int i = 0; i < VertBuff.size(); i++)
	{
		Vert_Buff_DX[StringNum]->CopyData(i, VertBuff[i]);
	}

	//устанавливаем текстуру
	ID3D12DescriptorHeap* descriptorHeaps[] = { m_SrvDescriptorHeap.Get() };
	m_CommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	m_CommandList->SetGraphicsRootDescriptorTable(0, m_SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	m_CommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	//устанавливаем вершинный буфер
	D3D12_VERTEX_BUFFER_VIEW vbv;
	vbv.BufferLocation = Vert_Buff_DX[StringNum]->Resource()->GetGPUVirtualAddress();
	vbv.StrideInBytes = sizeof(Vertex);
	vbv.SizeInBytes = (UINT)VertBuff.size() * sizeof(Vertex);;

	m_CommandList->IASetVertexBuffers(0, 1, &vbv);

	//выводим вершинный буфер на экран- рисуем текст
	m_CommandList->DrawInstanced(VertBuff.size(), 1, 0, 0);
}

void CMeshManager::Update_MeshManager()
{

	float ElapsedTime = m_Timer.Get_Elapsed_Time();

	m_CurrFrameResourceIndex = (m_CurrFrameResourceIndex + 1) % m_NumFrameResources;
	m_CurrFrameResource = m_FrameResources[m_CurrFrameResourceIndex].get();

	if (m_CurrFrameResource->Fence != 0 && m_Fence->GetCompletedValue() < m_CurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

void CMeshManager::Draw_MeshManager()
{
	auto CmdListAlloc = m_CurrFrameResource->CmdListAlloc;

	ThrowIfFailed(CmdListAlloc->Reset());

	ThrowIfFailed(m_CommandList->Reset(CmdListAlloc.Get(), m_PSO.Get()));
	
	m_CommandList->RSSetViewports(1, &m_ScreenViewport);
	m_CommandList->RSSetScissorRects(1, &m_ScissorRect);

	//------------------------------
	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	float ClearColor1[4] = { 0.0f, 0.125f, 0.3f, 1.0f };
	m_CommandList->ClearRenderTargetView(CurrentBackBufferView(), ClearColor1, 0, nullptr);
	m_CommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	m_CommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());

	
	//D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_ObjectCB->Resource()->GetGPUVirtualAddress();
	//m_CommandList->SetGraphicsRootConstantBufferView(1, cbAddress);

	char buff[256];

	//char width/height in screen coordinates
	float char_w = 10.0f;
	float char_h = 20.0f;

	std::string AdapterNameBuff;
	//переводим wchar_t в char для функции Print_Text()
	std::transform(StrAdapterName.begin(), StrAdapterName.end(), std::back_inserter(AdapterNameBuff), [](wchar_t c) {
		return (char)c;
		});

	sprintf_s(buff, 256, "Vendor %s", AdapterNameBuff.c_str());
	Print_Text(0, buff, 100.0f, 100.0f + char_h * 0.0f, char_w, char_h);
		
	unsigned int UnisgnedIntFPS = m_Timer.Calculate_FPS();

	sprintf_s(buff, 256, "FPS %d", UnisgnedIntFPS);
	Print_Text(1, buff, 100.0f, 100.0f + char_h * 2.0f, char_w, char_h);
	

	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
		
	ThrowIfFailed(m_CommandList->Close());

	ID3D12CommandList* cmdsLists[] = { m_CommandList.Get() };
	m_CommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(m_SwapChain->Present(0, 0));

	m_CurrBackBuffer = (m_CurrBackBuffer + 1) % m_SwapChainBufferCount;

	FlushCommandQueue();
}


