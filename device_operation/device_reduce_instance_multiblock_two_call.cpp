#include "device_reduce_instance_impl_common.hpp"
#include "device_reduce_multiblock_two_call.hpp"
#include "reduction_operator.hpp"

namespace ck {
namespace tensor_operation {
namespace device {
namespace device_reduce_instance {

template <typename compType, ReduceTensorOp_t reduceOp>
using deviceReducePtrType =
    DeviceReducePtr<typename reduce_unary_operator<compType, reduceOp, true, false>::preUnaryOp,
                    typename reduce_unary_operator<compType, reduceOp, true, false>::posUnaryOp>;

template <typename inType,
          typename compType,
          typename outType,
          int rank,
          typename toReduceDims,
          ReduceTensorOp_t reduceOp,
          NanPropagation_t nanOpt,
          ReduceTensorIndices_t indicesOpt>
void add_device_reduce_instance_multiblock_two_call(
    std::vector<deviceReducePtrType<compType, reduceOp>>& device_op_instances)
{
    using opReduce = typename reduce_binary_operator<compType, reduceOp>::opType;
    using preUnaryOpType =
        typename reduce_unary_operator<compType, reduceOp, true, false>::preUnaryOp;
    using posUnaryOpType =
        typename reduce_unary_operator<compType, reduceOp, true, false>::posUnaryOp;

    constexpr bool is_indexable =
        (reduceOp == ReduceTensorOp_t::MIN || reduceOp == ReduceTensorOp_t::MAX ||
         reduceOp == ReduceTensorOp_t::AMAX);
    constexpr bool need_indices = is_indexable && (indicesOpt != ReduceTensorIndices_t::NO_INDICES);

    constexpr bool propagate_nan = (nanOpt == NanPropagation_t::NOT_PROPAGATE_NAN) ? false : true;

    static_for<0, std::tuple_size<reduce_configuration_1_instances>::value, 1>{}([&](auto i) {
        using cfg1 =
            remove_cvref_t<decltype(std::get<i.value>(reduce_configuration_1_instances{}))>;

        static_for<0, std::tuple_size<reduce_configuration_2_instances>::value, 1>{}([&](auto j) {
            using cfg2 =
                remove_cvref_t<decltype(std::get<j.value>(reduce_configuration_2_instances{}))>;

            using ReduceOpInstance = DeviceReduceMultiBlockTwoCall<inType,
                                                                   compType,
                                                                   outType,
                                                                   rank,
                                                                   toReduceDims,
                                                                   opReduce,
                                                                   preUnaryOpType,
                                                                   posUnaryOpType,
                                                                   propagate_nan,
                                                                   need_indices,
                                                                   cfg1::blockSize_,
                                                                   cfg1::dim0_thread_cluster_size_,
                                                                   cfg1::dim1_thread_cluster_size_,
                                                                   cfg2::vectorDim_,
                                                                   cfg2::dim0_thread_slice_size_,
                                                                   cfg2::dim1_thread_slice_size_>;

            device_op_instances.push_back(std::make_unique<ReduceOpInstance>(ReduceOpInstance{}));
        });
    });
};

#define ADD_MULTIBLOCK_TWO_CALL_INST_BY_TYPE(                                           \
    inT, compT, outT, reduceOp, nanOpt, indicesOpt, rank, ...)                          \
    template void add_device_reduce_instance_multiblock_two_call<inT,                   \
                                                                 compT,                 \
                                                                 outT,                  \
                                                                 rank,                  \
                                                                 Sequence<__VA_ARGS__>, \
                                                                 reduceOp,              \
                                                                 nanOpt,                \
                                                                 indicesOpt>(           \
        std::vector<deviceReducePtrType<compT, reduceOp>> & device_op_instances)

#define ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(                                              \
    inT, compT, outT, reduceOp, nanOpt, indicesOpt, rank, ...)                           \
    ADD_MULTIBLOCK_TWO_CALL_INST_BY_TYPE(inT,                                            \
                                         compT,                                          \
                                         outT,                                           \
                                         static_cast<ReduceTensorOp_t>(reduceOp),        \
                                         static_cast<NanPropagation_t>(nanOpt),          \
                                         static_cast<ReduceTensorIndices_t>(indicesOpt), \
                                         rank,                                           \
                                         __VA_ARGS__)

// half, half, half
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 2, 0, 0, 4, 0, 1, 2); // for MIN
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 2, 0, 0, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 2, 0, 0, 2, 1);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 3, 0, 0, 4, 0, 1, 2); // for MAX
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 3, 0, 0, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 3, 0, 0, 2, 1);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 4, 0, 0, 4, 0, 1, 2); // for AMAX
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 4, 0, 0, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 4, 0, 0, 2, 1);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 2, 0, 1, 4, 0, 1, 2); // for MIN
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 2, 0, 1, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 2, 0, 1, 2, 1);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 3, 0, 1, 4, 0, 1, 2); // for MAX
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 3, 0, 1, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 3, 0, 1, 2, 1);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 4, 0, 1, 4, 0, 1, 2); // for AMAX
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 4, 0, 1, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, half_t, half_t, 4, 0, 1, 2, 1);       //

// half, float, half
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, float, half_t, 0, 0, 0, 4, 0, 1, 2); // for ADD
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, float, half_t, 0, 0, 0, 4, 0);
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, float, half_t, 0, 0, 0, 2, 1);
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, float, half_t, 5, 0, 0, 4, 0, 1, 2); // for AVG
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, float, half_t, 5, 0, 0, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, float, half_t, 5, 0, 0, 2, 1);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, float, half_t, 7, 0, 0, 4, 0, 1, 2); // for NORM2
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, float, half_t, 7, 0, 0, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(half_t, float, half_t, 7, 0, 0, 2, 1);       //

// float, float, float
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 2, 0, 0, 4, 0, 1, 2); // for MIN
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 2, 0, 0, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 2, 0, 0, 2, 1);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 3, 0, 0, 4, 0, 1, 2); // for MAX
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 3, 0, 0, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 3, 0, 0, 2, 1);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 4, 0, 0, 4, 0, 1, 2); // for AMAX
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 4, 0, 0, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 4, 0, 0, 2, 1);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 2, 0, 1, 4, 0, 1, 2); // for MIN
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 2, 0, 1, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 2, 0, 1, 2, 1);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 3, 0, 1, 4, 0, 1, 2); // for MAX
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 3, 0, 1, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 3, 0, 1, 2, 1);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 4, 0, 1, 4, 0, 1, 2); // for AMAX
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 4, 0, 1, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 4, 0, 1, 2, 1);       //

// float, float, float
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 7, 0, 0, 4, 0, 1, 2); // for NORM2
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 7, 0, 0, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, float, float, 7, 0, 0, 2, 1);       //

// float, double, float
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, double, float, 7, 0, 0, 4, 0, 1, 2); // for NORM2
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, double, float, 7, 0, 0, 4, 0);       //
ADD_MULTIBLOCK_TWO_CALL_INST_BY_ID(float, double, float, 7, 0, 0, 2, 1);       //

} // namespace device_reduce_instance
} // namespace device
} // namespace tensor_operation

} // namespace ck
