#ifndef DRIVER_CONVOLUTION_FORWARD_IMPLICIT_GEMM_V5R1_DLOPS_NHWC_KYXC_NHWK_HPP
#define DRIVER_CONVOLUTION_FORWARD_IMPLICIT_GEMM_V5R1_DLOPS_NHWC_KYXC_NHWK_HPP

#include "common_header.hpp"
#include "tensor_descriptor.hpp"
#include "tensor_descriptor_helper.hpp"
#include "gridwise_gemm_dlops_v2.hpp"

template <ck::index_t BlockSize,
          typename FloatAB,
          typename FloatAcc,
          typename FloatC,
          ck::index_t E1_,
          ck::index_t E2_,
          ck::index_t KPerBlock,
          ck::index_t HoPerBlock,
          ck::index_t WoPerBlock,
          ck::index_t E1PerBlock,
          ck::index_t KPerThread,
          ck::index_t HoPerThread,
          ck::index_t WoPerThread,
          ck::index_t EPerThread,
          typename ABlockTransferThreadSliceLengths_E0_E1_K_E2,
          typename ABlockTransferThreadClusterLengths_E0_E1_K_E2,
          ck::index_t ABlockTransferSrcScalarPerVector_E2,
          ck::index_t ABlockTransferDstScalarPerVector_E2,
          ck::index_t BThreadTransferSrcScalarPerVector_E2,
          ck::index_t CThreadTransferDstScalarPerVector_K>
struct DriverDynamicConvolutionForwardImplicitGemmDlops_v5r1_nhwc_kyxc_nhwk_outpad
{
    template <typename... Wei,
              typename... In,
              typename... Out,
              typename ConvStrides,
              typename ConvDilations,
              typename InLeftPads,
              typename InRightPads>
    __host__ float Run(const ck::TensorDescriptor<Wei...>& wei_k_y_x_c_global_desc,
                       const ck::TensorDescriptor<In...>& in_n_hi_wi_c_global_desc,
                       const ck::TensorDescriptor<Out...>& out_n_ho_wo_k_global_desc,
                       const ConvStrides& conv_strides,
                       const ConvDilations& conv_dilations,
                       const InLeftPads& in_left_pads,
                       const InRightPads& in_right_pads,
                       const FloatAB* __restrict__ p_a_grid,
                       const FloatAB* __restrict__ p_b_grid,
                       FloatC* __restrict__ p_c_grid,
                       const int nrepeat) const
    {
        using namespace ck;

        constexpr auto I0 = Number<0>{};
        constexpr auto I1 = Number<1>{};
        constexpr auto I2 = Number<2>{};
        constexpr auto I3 = Number<3>{};

        const auto N  = in_n_hi_wi_c_global_desc.GetLength(I0);
        const auto Hi = in_n_hi_wi_c_global_desc.GetLength(I1);
        const auto Wi = in_n_hi_wi_c_global_desc.GetLength(I2);
        const auto C  = in_n_hi_wi_c_global_desc.GetLength(I3);

        const auto Ho = out_n_ho_wo_k_global_desc.GetLength(I1);
        const auto Wo = out_n_ho_wo_k_global_desc.GetLength(I2);
        const auto K  = out_n_ho_wo_k_global_desc.GetLength(I3);

        const auto Y = wei_k_y_x_c_global_desc.GetLength(I1);
        const auto X = wei_k_y_x_c_global_desc.GetLength(I2);

        const auto ConvStrideH = conv_strides[I0];
        const auto ConvStrideW = conv_strides[I1];

        const auto ConvDilationH = conv_dilations[I0];
        const auto ConvDilationW = conv_dilations[I1];

        const auto Hop = (Ho + HoPerBlock - 1) / HoPerBlock * HoPerBlock;
        const auto Wop = (Wo + WoPerBlock - 1) / WoPerBlock * WoPerBlock;

        const auto OutRightPadH = Hop - Ho;
        const auto OutRightPadW = Wop - Wo;

        const auto InLeftPadH = in_left_pads[I0];
        const auto InLeftPadW = in_left_pads[I1];

        const auto InRightPadH = in_right_pads[I0] + OutRightPadH * ConvStrideH;
        const auto InRightPadW = in_right_pads[I1] + OutRightPadW * ConvStrideW;

        std::cerr << "OutRightPadH = " << OutRightPadH << " OutRightPadW = " << OutRightPadW
                  << std::endl;
        std::cerr << "InRightPadH = " << InRightPadH << " InRightPadW = " << InRightPadW
                  << std::endl;

        constexpr auto E1 = Number<E1_>{};
        constexpr auto E2 = Number<E2_>{};

        const auto C0 = C / E2;
        const auto E  = Y * X * C0;
        const auto E0 = E / E1;

        // weight tensor
        const auto a_e_k_e2_grid_desc =
            transform_tensor_descriptor(make_naive_tensor_descriptor_packed(make_tuple(K, E, E2)),
                                        make_tuple(make_pass_through_transform(K),
                                                   make_pass_through_transform(E),
                                                   make_pass_through_transform(E2)),
                                        make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}),
                                        make_tuple(Sequence<1>{}, Sequence<0>{}, Sequence<2>{}));

        const auto a_e0_e1_k_e2_grid_desc =
            transform_tensor_descriptor(a_e_k_e2_grid_desc,
                                        make_tuple(make_unmerge_transform(make_tuple(E0, E1)),
                                                   make_pass_through_transform(K),
                                                   make_pass_through_transform(E2)),
                                        make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}),
                                        make_tuple(Sequence<0, 1>{}, Sequence<2>{}, Sequence<3>{}));

        // input tensor
        const auto in_n_hip_wip_c0_e2_global_desc = transform_tensor_descriptor(
            make_naive_tensor_descriptor_packed(make_tuple(N, Hi, Wi, C0, E2)),
            make_tuple(make_pass_through_transform(N),
                       make_pad_transform(Hi, InLeftPadH, InRightPadH),
                       make_pad_transform(Wi, InLeftPadW, InRightPadW),
                       make_pass_through_transform(C0),
                       make_pass_through_transform(E2)),
            make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}, Sequence<3>{}, Sequence<4>{}),
            make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}, Sequence<3>{}, Sequence<4>{}));

        const auto in_n_y_ho_x_wo_c0_e2_global_desc = transform_tensor_descriptor(
            in_n_hip_wip_c0_e2_global_desc,
            make_tuple(
                make_pass_through_transform(N),
                make_embed_transform(make_tuple(Y, Hop), make_tuple(ConvDilationH, ConvStrideH)),
                make_embed_transform(make_tuple(X, Wop), make_tuple(ConvDilationW, ConvStrideW)),
                make_pass_through_transform(C0),
                make_pass_through_transform(E2)),
            make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}, Sequence<3>{}, Sequence<4>{}),
            make_tuple(
                Sequence<0>{}, Sequence<1, 2>{}, Sequence<3, 4>{}, Sequence<5>{}, Sequence<6>{}));

        const auto b_e_n_ho_wo_e2_grid_desc = transform_tensor_descriptor(
            in_n_y_ho_x_wo_c0_e2_global_desc,
            make_tuple(make_merge_transform(make_tuple(Y, X, C0)),
                       make_pass_through_transform(N),
                       make_pass_through_transform(Hop),
                       make_pass_through_transform(Wop),
                       make_pass_through_transform(E2)),
            make_tuple(
                Sequence<1, 3, 5>{}, Sequence<0>{}, Sequence<2>{}, Sequence<4>{}, Sequence<6>{}),
            make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}, Sequence<3>{}, Sequence<4>{}));

        const auto b_e0_e1_n_ho_wo_e2_grid_desc = transform_tensor_descriptor(
            b_e_n_ho_wo_e2_grid_desc,
            make_tuple(make_unmerge_transform(make_tuple(E0, E1)),
                       make_pass_through_transform(N),
                       make_pass_through_transform(Hop),
                       make_pass_through_transform(Wop),
                       make_pass_through_transform(E2)),
            make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}, Sequence<3>{}, Sequence<4>{}),
            make_tuple(
                Sequence<0, 1>{}, Sequence<2>{}, Sequence<3>{}, Sequence<4>{}, Sequence<5>{}));

        // output tensor
        const auto c_k_n_hop_wop_grid_desc = transform_tensor_descriptor(
            out_n_ho_wo_k_global_desc,
            make_tuple(make_pass_through_transform(N),
                       make_pad_transform(Ho, 0, OutRightPadH),
                       make_pad_transform(Wo, 0, OutRightPadW),
                       make_pass_through_transform(K)),
            make_tuple(Sequence<3>{}, Sequence<0>{}, Sequence<1>{}, Sequence<2>{}),
            make_tuple(Sequence<0>{}, Sequence<1>{}, Sequence<2>{}, Sequence<3>{}));

        std::cerr << "Hop = " << Hop << " Wop = " << Wop << std::endl;

        if(!((K % KPerBlock) == 0 && (Hop % HoPerBlock) == 0 && (Wop % WoPerBlock) == 0 &&
             (E1 % E1PerBlock) == 0))
        {
            throw std::runtime_error("wrong! GEMM size no divisible");
        }

        // hack to control index calculation when iterating over a_k_m_global tensor
        constexpr auto a_e0_e1_k_e2_global_step_hacks =
            make_tuple(make_tuple(Sequence<0, 0, 0, 0, 0, 0, 0>{},
                                  Sequence<0, 0, 0, 0, 0, 0, 0>{},
                                  Sequence<0, 0, 0, 0, 0, 0, 0>{},
                                  Sequence<0, 0, 0, 0, 0, 0, 0>{}),
                       make_tuple(Sequence<0, 0, 0, 0, 0, 0, 0>{},
                                  Sequence<0, 0, 0, 0, 0, 0, 0>{},
                                  Sequence<0, 0, 0, 0, 0, 0, 0>{},
                                  Sequence<0, 0, 0, 0, 0, 0, 0>{}));

        constexpr auto a_e0_e1_k_e2_global_move_slice_window_step_hack =
            Sequence<0, 0, 0, 0, 0, 0, 0>{};

        constexpr auto b_e0_e1_n_ho_wo_e2_global_step_hacks = make_tuple(
            make_tuple(Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},
                       Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},
                       Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},
                       Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},
                       Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},
                       Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{}),
            make_tuple(Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},
                       Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},
                       Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},
                       Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},
                       Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{},
                       Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0>{}));

        constexpr auto b_e0_e1_n_ho_wo_e2_global_move_slice_window_step_hack =
            Sequence<0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0>{};

        // hack to control index calculation when iterating over c_m0_m1_n0_n1_global tensor
        // hack for NKHW format
        constexpr auto c_k_n_ho_wo_global_tensor_step_hacks =
            make_tuple(make_tuple(Sequence<0, 0, 0, 0, 0>{},
                                  Sequence<0, 0, 0, 0, 0>{},
                                  Sequence<0, 0, 0, 0, 0>{},
                                  Sequence<0, 0, 0, 0, 0>{}),
                       make_tuple(Sequence<0, 0, 0, 0, 0>{},
                                  Sequence<0, 0, 0, 0, 0>{},
                                  Sequence<0, 0, 0, 0, 0>{},
                                  Sequence<0, 0, 0, 0, 0>{}));

        // GEMM
        using GridwiseGemm = GridwiseGemmDlops_km_kn_mn_v3<
            BlockSize,
            FloatAB,
            FloatAcc,
            FloatC,
            InMemoryDataOperationEnum_t::Set,
            decltype(a_e0_e1_k_e2_grid_desc),
            decltype(b_e0_e1_n_ho_wo_e2_grid_desc),
            decltype(c_k_n_hop_wop_grid_desc),
            E1,
            E2,
            KPerBlock,
            HoPerBlock,
            WoPerBlock,
            E1PerBlock,
            KPerThread,
            HoPerThread,
            WoPerThread,
            EPerThread,
            ABlockTransferThreadSliceLengths_E0_E1_K_E2,
            ABlockTransferThreadClusterLengths_E0_E1_K_E2,
            Sequence<0, 1, 2, 3>,
            Sequence<0, 1, 2, 3>,
            3,
            ABlockTransferSrcScalarPerVector_E2,
            ABlockTransferDstScalarPerVector_E2,
            false, // don't move back src coordinate after threadwise copy
            Sequence<2, 0, 1, 3, 4, 5>,
            5,
            BThreadTransferSrcScalarPerVector_E2,
            false, // don't move back src coordinate after threadwise copy, which will be fused with
                   // MoveSrcSliceWindow() to save addr computation
            Sequence<1, 2, 3, 0>,
            0,
            CThreadTransferDstScalarPerVector_K,
            decltype(a_e0_e1_k_e2_global_step_hacks),
            decltype(b_e0_e1_n_ho_wo_e2_global_step_hacks),
            decltype(c_k_n_ho_wo_global_tensor_step_hacks),
            decltype(a_e0_e1_k_e2_global_move_slice_window_step_hack),
            decltype(b_e0_e1_n_ho_wo_e2_global_move_slice_window_step_hack)>;

        using AGridDesc_E0_E1_K_E2       = decltype(a_e0_e1_k_e2_grid_desc);
        using BGridDesc_E0_E1_N_Ho_Wo_E2 = decltype(b_e0_e1_n_ho_wo_e2_grid_desc);
        using CGridDesc_K_N_Ho_Wo        = decltype(c_k_n_hop_wop_grid_desc);

        const auto grid_size = (K / KPerBlock) * (Hop / HoPerBlock) * (Wop / WoPerBlock) * N;

        constexpr bool has_main_k_block_loop        = (E1 + E1PerBlock) / (2 * E1PerBlock) > 1;
        constexpr bool has_double_tail_k_block_loop = (E1 / E1PerBlock) % 2 == 0;

        const bool has_e0_block_loop = E0 > 1;

        std::cerr << "has_main_k_block_loop = " << has_main_k_block_loop
                  << " has_double_tail_k_block_loop = " << has_double_tail_k_block_loop
                  << " has_e0_block_loop = " << has_e0_block_loop << std::endl;

        const auto c_blockid_to_k_n_ho_wo_block_cluster_adaptor =
            make_single_stage_tensor_adaptor(make_tuple(make_pass_through_transform(I0)),
                                             make_tuple(Sequence<0>{}),
                                             make_tuple(Sequence<0>{}));

        using CBlockIdToBlockClusterAdaptor_K_N_Ho_Wo =
            decltype(c_blockid_to_k_n_ho_wo_block_cluster_adaptor);

#if CK_EXPERIMENTAL_PASS_TENSOR_DESCRIPTOR_BY_VALUE
        float ave_time = 0;

        if constexpr(has_main_k_block_loop && has_double_tail_k_block_loop)
        {
            const auto kernel =
                kernel_gemm_dlops_v2<GridwiseGemm,
                                     FloatAB,
                                     FloatC,
                                     remove_reference_t<AGridDesc_E0_E1_K_E2>,
                                     remove_reference_t<BGridDesc_E0_E1_N_Ho_Wo_E2>,
                                     remove_reference_t<CGridDesc_K_N_Ho_Wo>,
                                     remove_reference_t<CBlockIdToBlockClusterAdaptor_K_N_Ho_Wo>,
                                     true,
                                     true>;

            ave_time = launch_and_time_kernel(kernel,
                                              nrepeat,
                                              dim3(grid_size),
                                              dim3(BlockSize),
                                              0,
                                              p_a_grid,
                                              p_b_grid,
                                              p_c_grid,
                                              a_e0_e1_k_e2_grid_desc,
                                              b_e0_e1_n_ho_wo_e2_grid_desc,
                                              c_k_n_hop_wop_grid_desc,
                                              c_blockid_to_k_n_ho_wo_block_cluster_adaptor);
        }
        else if constexpr(has_main_k_block_loop && !has_double_tail_k_block_loop)
        {
            const auto kernel =
                kernel_gemm_dlops_v2<GridwiseGemm,
                                     FloatAB,
                                     FloatC,
                                     remove_reference_t<AGridDesc_E0_E1_K_E2>,
                                     remove_reference_t<BGridDesc_E0_E1_N_Ho_Wo_E2>,
                                     remove_reference_t<CGridDesc_K_N_Ho_Wo>,
                                     remove_reference_t<CBlockIdToBlockClusterAdaptor_K_N_Ho_Wo>,
                                     true,
                                     false>;

            ave_time = launch_and_time_kernel(kernel,
                                              nrepeat,
                                              dim3(grid_size),
                                              dim3(BlockSize),
                                              0,
                                              p_a_grid,
                                              p_b_grid,
                                              p_c_grid,
                                              a_e0_e1_k_e2_grid_desc,
                                              b_e0_e1_n_ho_wo_e2_grid_desc,
                                              c_k_n_hop_wop_grid_desc,
                                              c_blockid_to_k_n_ho_wo_block_cluster_adaptor);
        }
        else if constexpr(!has_main_k_block_loop && has_double_tail_k_block_loop)
        {
            const auto kernel =
                kernel_gemm_dlops_v2<GridwiseGemm,
                                     FloatAB,
                                     FloatC,
                                     remove_reference_t<AGridDesc_E0_E1_K_E2>,
                                     remove_reference_t<BGridDesc_E0_E1_N_Ho_Wo_E2>,
                                     remove_reference_t<CGridDesc_K_N_Ho_Wo>,
                                     remove_reference_t<CBlockIdToBlockClusterAdaptor_K_N_Ho_Wo>,
                                     false,
                                     true>;

            ave_time = launch_and_time_kernel(kernel,
                                              nrepeat,
                                              dim3(grid_size),
                                              dim3(BlockSize),
                                              0,
                                              p_a_grid,
                                              p_b_grid,
                                              p_c_grid,
                                              a_e0_e1_k_e2_grid_desc,
                                              b_e0_e1_n_ho_wo_e2_grid_desc,
                                              c_k_n_hop_wop_grid_desc,
                                              c_blockid_to_k_n_ho_wo_block_cluster_adaptor);
        }
        else
        {
            const auto kernel =
                kernel_gemm_dlops_v2<GridwiseGemm,
                                     FloatAB,
                                     FloatC,
                                     remove_reference_t<AGridDesc_E0_E1_K_E2>,
                                     remove_reference_t<BGridDesc_E0_E1_N_Ho_Wo_E2>,
                                     remove_reference_t<CGridDesc_K_N_Ho_Wo>,
                                     remove_reference_t<CBlockIdToBlockClusterAdaptor_K_N_Ho_Wo>,
                                     false,
                                     false>;

            ave_time = launch_and_time_kernel(kernel,
                                              nrepeat,
                                              dim3(grid_size),
                                              dim3(BlockSize),
                                              0,
                                              p_a_grid,
                                              p_b_grid,
                                              p_c_grid,
                                              a_e0_e1_k_e2_grid_desc,
                                              b_e0_e1_n_ho_wo_e2_grid_desc,
                                              c_k_n_hop_wop_grid_desc,
                                              c_blockid_to_k_n_ho_wo_block_cluster_adaptor);
        }

        return ave_time;
#elif CK_EXPERIMENTAL_PASS_TENSOR_DESCRIPTOR_BY_VOID_POINTER
        DeviceMem a_e0_e1_k_e2_grid_desc_dev_buf(sizeof(AGridDesc_E0_E1_K_E2));
        DeviceMem b_e0_e1_n_ho_wo_e2_grid_desc_dev_buf(sizeof(BGridDesc_E0_E1_N_Ho_Wo_E2));
        DeviceMem c_k_n_hop_wop_grid_desc_dev_buf(sizeof(CGridDesc_K_N_Ho_Wo));
        DeviceMem c_blockid_to_k_n_ho_wo_block_cluster_adaptor_dev_buf(
            sizeof(CBlockIdToBlockClusterAdaptor_K_N_Ho_Wo));

        a_e0_e1_k_e2_grid_desc_dev_buf.ToDevice(&a_e0_e1_k_e2_grid_desc);
        b_e0_e1_n_ho_wo_e2_grid_desc_dev_buf.ToDevice(&b_e0_e1_n_ho_wo_e2_grid_desc);
        c_k_n_hop_wop_grid_desc_dev_buf.ToDevice(&c_k_n_hop_wop_grid_desc);
        c_blockid_to_k_n_ho_wo_block_cluster_adaptor_dev_buf.ToDevice(
            &c_blockid_to_k_n_ho_wo_block_cluster_adaptor);

        float ave_time = 0;

        if constexpr(has_main_k_block_loop && has_double_tail_k_block_loop)
        {
            const auto kernel =
                kernel_gemm_dlops_v2<GridwiseGemm,
                                     FloatAB,
                                     FloatC,
                                     remove_reference_t<AGridDesc_E0_E1_K_E2>,
                                     remove_reference_t<BGridDesc_E0_E1_N_Ho_Wo_E2>,
                                     remove_reference_t<CGridDesc_K_N_Ho_Wo>,
                                     remove_reference_t<CBlockIdToBlockClusterAdaptor_K_N_Ho_Wo>,
                                     true,
                                     true>;

            ave_time = launch_and_time_kernel(
                kernel,
                nrepeat,
                dim3(grid_size),
                dim3(BlockSize),
                0,
                p_a_grid,
                p_b_grid,
                p_c_grid,
                cast_pointer_to_constant_address_space(
                    a_e0_e1_k_e2_grid_desc_dev_buf.GetDeviceBuffer()),
                cast_pointer_to_constant_address_space(
                    b_e0_e1_n_ho_wo_e2_grid_desc_dev_buf.GetDeviceBuffer()),
                cast_pointer_to_constant_address_space(
                    c_k_n_hop_wop_grid_desc_dev_buf.GetDeviceBuffer()),
                cast_pointer_to_constant_address_space(
                    c_blockid_to_k_n_ho_wo_block_cluster_adaptor_dev_buf.GetDeviceBuffer()));
        }
        else if constexpr(has_main_k_block_loop && !has_double_tail_k_block_loop)
        {
            const auto kernel =
                kernel_gemm_dlops_v2<GridwiseGemm,
                                     FloatAB,
                                     FloatC,
                                     remove_reference_t<AGridDesc_E0_E1_K_E2>,
                                     remove_reference_t<BGridDesc_E0_E1_N_Ho_Wo_E2>,
                                     remove_reference_t<CGridDesc_K_N_Ho_Wo>,
                                     remove_reference_t<CBlockIdToBlockClusterAdaptor_K_N_Ho_Wo>,
                                     true,
                                     false>;

            ave_time = launch_and_time_kernel(
                kernel,
                nrepeat,
                dim3(grid_size),
                dim3(BlockSize),
                0,
                p_a_grid,
                p_b_grid,
                p_c_grid,
                cast_pointer_to_constant_address_space(
                    a_e0_e1_k_e2_grid_desc_dev_buf.GetDeviceBuffer()),
                cast_pointer_to_constant_address_space(
                    b_e0_e1_n_ho_wo_e2_grid_desc_dev_buf.GetDeviceBuffer()),
                cast_pointer_to_constant_address_space(
                    c_k_n_hop_wop_grid_desc_dev_buf.GetDeviceBuffer()),
                cast_pointer_to_constant_address_space(
                    c_blockid_to_k_n_ho_wo_block_cluster_adaptor_dev_buf.GetDeviceBuffer()));
        }
        else if constexpr(!has_main_k_block_loop && has_double_tail_k_block_loop)
        {
            const auto kernel =
                kernel_gemm_dlops_v2<GridwiseGemm,
                                     FloatAB,
                                     FloatC,
                                     remove_reference_t<AGridDesc_E0_E1_K_E2>,
                                     remove_reference_t<BGridDesc_E0_E1_N_Ho_Wo_E2>,
                                     remove_reference_t<CGridDesc_K_N_Ho_Wo>,
                                     remove_reference_t<CBlockIdToBlockClusterAdaptor_K_N_Ho_Wo>,
                                     false,
                                     true>;

            ave_time = launch_and_time_kernel(
                kernel,
                nrepeat,
                dim3(grid_size),
                dim3(BlockSize),
                0,
                p_a_grid,
                p_b_grid,
                p_c_grid,
                cast_pointer_to_constant_address_space(
                    a_e0_e1_k_e2_grid_desc_dev_buf.GetDeviceBuffer()),
                cast_pointer_to_constant_address_space(
                    b_e0_e1_n_ho_wo_e2_grid_desc_dev_buf.GetDeviceBuffer()),
                cast_pointer_to_constant_address_space(
                    c_k_n_hop_wop_grid_desc_dev_buf.GetDeviceBuffer()),
                cast_pointer_to_constant_address_space(
                    c_blockid_to_k_n_ho_wo_block_cluster_adaptor_dev_buf.GetDeviceBuffer()));
        }
        else
        {
            const auto kernel =
                kernel_gemm_dlops_v2<GridwiseGemm,
                                     FloatAB,
                                     FloatC,
                                     remove_reference_t<AGridDesc_E0_E1_K_E2>,
                                     remove_reference_t<BGridDesc_E0_E1_N_Ho_Wo_E2>,
                                     remove_reference_t<CGridDesc_K_N_Ho_Wo>,
                                     remove_reference_t<CBlockIdToBlockClusterAdaptor_K_N_Ho_Wo>,
                                     false,
                                     false>;

            ave_time = launch_and_time_kernel(
                kernel,
                nrepeat,
                dim3(grid_size),
                dim3(BlockSize),
                0,
                p_a_grid,
                p_b_grid,
                p_c_grid,
                cast_pointer_to_constant_address_space(
                    a_e0_e1_k_e2_grid_desc_dev_buf.GetDeviceBuffer()),
                cast_pointer_to_constant_address_space(
                    b_e0_e1_n_ho_wo_e2_grid_desc_dev_buf.GetDeviceBuffer()),
                cast_pointer_to_constant_address_space(
                    c_k_n_hop_wop_grid_desc_dev_buf.GetDeviceBuffer()),
                cast_pointer_to_constant_address_space(
                    c_blockid_to_k_n_ho_wo_block_cluster_adaptor_dev_buf.GetDeviceBuffer()));
        }

        return ave_time;
#endif
    }
};
#endif