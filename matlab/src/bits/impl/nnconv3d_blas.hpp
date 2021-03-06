// @file nnconv3d_blas.hpp
// @brief 3D Convolution block BLAS-based implementation.
// @author Tuan-Hung VU

/*
Copyright (C) 2016 Tuan-Hung VU
*/

#ifndef __vl__nnconv3d_blas__
#define __vl__nnconv3d_blas__

//#include "im2row.hpp"
#include "vol2row.hpp"
#include "blashelper.hpp"
#include <assert.h>

namespace vl { namespace impl {

  template<vl::Device deviceType, vl::Type dataType> inline vl::Error
  nnconv3d_forward_blas(Context& context,
                      Tensor output, double outputMult,
                      Tensor data, double dataMult,
                      Tensor filters,
                      Tensor biases,
                      int strideY, int strideX, int strideT,
                      int padTop, int padBottom,
                      int padLeft, int padRight, int padT) ;

  template<vl::Device deviceType, vl::Type dataType> inline vl::Error
  nnconv3d_backward_blas(Context& context,
                       Tensor derData,
                       Tensor derFilters,
                       Tensor derBiases,
                       Tensor data,
                       Tensor filters,
                       Tensor derOutput,
                       int strideY, int strideX, int strideT,
                       int padTop, int padBottom,
                       int padLeft, int padRight, int padT) ;

} }

/*

 One image at a time is processed.

 Filters are (optionally) divided in to groups, one for each group of dimensions.


                 patchVolume                  numFilters
                 +-------------------------+   +-----------------------+

                 filtersVolume              numFiltersPerGroup
                 +------------+------------+   +-----------+-----------+      +--------+--------+
                 |            |            |   |           |           |      |        |        |
                 |            |            |   |  filter   |           |      |        |        |
                 |            |            |   |  group 1  |     0     |  =   |        |        |
                 |            |            |   |           |           |      |        |        |
                 |            |            |   |           |           |      |        |        |
                 |            |            |   +-----------------------+      |        |        |
 numOutputPixels |   grp. 1   |   grp. 2   |   |           |           |      |        |        |
                 |            |            |   |           |  filter   |      |        |        |
                 |            |            |   |     0     |  group 2  |      |        |        |
                 |            |            |   |           |           |      |        |        |
                 |            |            |   |           |           |      |        |        |
                 |            |            |   +-----------+-----------+      |        |        |
                 |            |            |                                  |        |        |
                 |            |            |            filters               |        |        |
                 |            |            |                                  |        |        |
                 +------------+------------+                                  +--------+--------+

                 temp                                                     output

 */

template<vl::Device deviceType, vl::Type dataType> inline vl::Error
vl::impl::nnconv3d_forward_blas(Context& context,
                              Tensor output, double outputMult,
                              Tensor data, double dataMult,
                              Tensor filters,
                              Tensor biases,
                              int strideY, int strideX, int strideT,
                              int padTop, int padBottom,
                              int padLeft, int padRight, int padT)
{
  assert(output) ;
  assert(data) ;
  assert(filters) ;

  vl::Error error ;
  typedef typename vl::DataTypeTraits<dataType>::type type ;


  ptrdiff_t numGroups = data.getDimension(3) / filters.getDimension(3) ;
  ptrdiff_t numFiltersPerGroup = filters.getDimension(4) / numGroups ;
  ptrdiff_t numOutputPixels = output.getDimension(0) * output.getDimension(1) * output.getDimension(2) ;
  ptrdiff_t filtersVolume = filters.getDimension(0) * filters.getDimension(1) * filters.getDimension(2) * filters.getDimension(3) ;
  ptrdiff_t tempVolume = numOutputPixels * filtersVolume * numGroups ;

  type* tempMemory = (type*) context.getWorkspace(deviceType, tempVolume * sizeof(type)) ;
  type const* allOnesMemory = (type*) context.getAllOnes(deviceType,
                                                         dataType,
                                                         numOutputPixels) ;
  if (tempMemory == NULL || allOnesMemory == NULL) {
    error = context.getLastError() ;
    goto done ;
  }

  for (int image = 0 ; image < data.getDimension(4) ; ++image) {

    ptrdiff_t dataOffset = (data.getDimension(0)*data.getDimension(1)*data.getDimension(2)*data.getDimension(3)) * image ;
    ptrdiff_t outputOffset = (output.getDimension(0)*output.getDimension(1)*output.getDimension(2)*output.getDimension(3)) * image ;

    error = vl::impl::vol2row<deviceType,type>(context,
                                        tempMemory,
                                        (type*)data.getMemory() + dataOffset,
                                        data.getDimension(0), data.getDimension(1), data.getDimension(2), data.getDimension(3),
                                        filters.getDimension(0), filters.getDimension(1), filters.getDimension(2),
                                        strideY, strideX, strideT,
                                        padTop, padBottom, padLeft, padRight, padT) ;
    if (error != vl::vlSuccess) { goto done ; }

    for (int g = 0 ; g < numGroups ; ++ g) {
      ptrdiff_t filterGrpOffset = filtersVolume * numFiltersPerGroup * g ;
      ptrdiff_t tempGrpOffset = numOutputPixels * filtersVolume * g ;
      ptrdiff_t outputGrpOffset = numOutputPixels * numFiltersPerGroup * g  ;
      type alpha = dataMult ;
      type beta = outputMult ;
      error = vl::impl::blas<deviceType,dataType>::gemm(context,
                              'n', 'n',
                              numOutputPixels, numFiltersPerGroup, filtersVolume,
                              alpha,
                              tempMemory + tempGrpOffset, numOutputPixels,
                              (type*)filters.getMemory() + filterGrpOffset, filtersVolume,
                              beta,
                              (type*)output.getMemory() + outputOffset + outputGrpOffset, numOutputPixels) ;
      if (error != vl::vlSuccess) { goto done ; }
    }

    if (biases) {
      type alpha = 1 ;
      type beta = 1 ;
      error = vl::impl::blas<deviceType,dataType>::gemm(context,
                              'n', 'n',
                              numOutputPixels, biases.getNumElements(), 1,
                              alpha,
                              allOnesMemory, numOutputPixels,
                              (type*)biases.getMemory(), 1,
                              beta,
                              (type*)output.getMemory() + outputOffset, numOutputPixels) ;
      if (error != vl::vlSuccess) { goto done ; }
    }
  }

done:
  return context.passError(error, "nnconv3d_forward_blas<>: ") ;
}

template<vl::Device deviceType, vl::Type dataType> inline vl::Error
vl::impl::nnconv3d_backward_blas(Context& context,
                               Tensor derData,
                               Tensor derFilters,
                               Tensor derBiases,
                               Tensor data,
                               Tensor filters,
                               Tensor derOutput,
                               int strideY, int strideX, int strideT,
                               int padTop, int padBottom,
                               int padLeft, int padRight, int padT)
{
  vl::Error error ;
  typedef typename vl::DataTypeTraits<dataType>::type type ;

  ptrdiff_t numGroups = 0 ;
  ptrdiff_t numFiltersPerGroup = 0 ;
  ptrdiff_t filtersVolume = 0 ;
  type const* allOnesMemory = NULL ;
  ptrdiff_t tempVolume = 0 ;
  type* tempMemory = NULL ;

  // for all derivatives
  assert(derOutput) ;
  ptrdiff_t numOutputPixels = derOutput.getDimension(0) * derOutput.getDimension(1) * derOutput.getDimension(2) ;
  
  if (derBiases) {
    // for derivative w.r.t. bias
    allOnesMemory = (type*) context.getAllOnes(deviceType,
                                               dataType,
                                               numOutputPixels) ;
    if (allOnesMemory == NULL) {
      error = context.getLastError() ;
      goto done ;
    }
  }

  if (derData) {
    // for derivative w.r.t. data
    assert(filters) ;
    numGroups = derData.getDimension(3) / filters.getDimension(3) ;
    filtersVolume = filters.getDimension(0) * filters.getDimension(1) * filters.getDimension(2) * filters.getDimension(3) ;
  }
  else if (derFilters) {
    // for derivative w.r.t. filters
    assert(data) ;
    numGroups = data.getDimension(3) / derFilters.getDimension(3) ;
    filtersVolume = derFilters.getDimension(0) * derFilters.getDimension(1) * derFilters.getDimension(2) * derFilters.getDimension(3) ;
  }
  numFiltersPerGroup = derOutput.getDimension(3) / numGroups ;

  // get scratch space
  tempVolume = numOutputPixels * filtersVolume * numGroups ;
  if (tempVolume) {
    tempMemory = (type*) context.getWorkspace(deviceType, tempVolume * sizeof(type)) ;
    if (tempMemory == NULL) {
      error = context.getLastError() ;
      goto done ;
    }
  }

  for (int image = 0 ; image < derOutput.getDimension(4) ; ++image) {

    ptrdiff_t derOutputOffset = (derOutput.getDimension(0)*derOutput.getDimension(1)*derOutput.getDimension(2)*derOutput.getDimension(3)) * image ;

    /* compute derData dz/dbias */
    if (derBiases) {
      // has derBiases, derOutput
      type alpha = 1 ;
      type beta = (image > 0) ; /* this saves init. the output array with 0 */
      error = vl::impl::blas<deviceType,dataType>::gemv(context,
                              't',
                              numOutputPixels, derOutput.getDepth(),
                              alpha, /* alpha */
                              (type const*)derOutput.getMemory() + derOutputOffset, numOutputPixels,
                              allOnesMemory, 1,
                              beta, /* beta */
                              (type*)derBiases.getMemory(), 1) ;
      if (error != vl::vlSuccess) { return error ; }
    }

    /* compute derData dz/dx */
    if (derData) {
      // has derData, derOutput, filters
      ptrdiff_t derDataOffset = (derData.getDimension(0)*derData.getDimension(1)*derData.getDimension(2)*derData.getDimension(3)) * image ;
      for (int g = 0 ; g < numGroups ; ++ g) {
        ptrdiff_t filterGrpOffset = filtersVolume * numFiltersPerGroup * g ;
        ptrdiff_t tempGrpOffset = numOutputPixels * filtersVolume * g ;
        ptrdiff_t derOutputGrpOffset = numOutputPixels * numFiltersPerGroup * g  ;
        float alpha = 1 ;
        float beta = 0 ;
        error = vl::impl::blas<deviceType,dataType>::gemm(context,
                                'n', 't',
                                numOutputPixels, filtersVolume, numFiltersPerGroup,
                                alpha,
                                (type*)derOutput.getMemory() + derOutputOffset + derOutputGrpOffset, numOutputPixels,
                                (type*)filters.getMemory() + filterGrpOffset, filtersVolume,
                                beta,
                                tempMemory + tempGrpOffset, numOutputPixels) ;
        if (error != vl::vlSuccess) { return error ; }
      }
      error = vl::impl::row2vol<deviceType,type>(context,
                                          (type*)derData.getMemory() + derDataOffset,
                                          tempMemory,
                                          derData.getDimension(0), derData.getDimension(1), derData.getDimension(2), derData.getDimension(3),
                                          filters.getDimension(0), filters.getDimension(1), filters.getDimension(2),
                                          strideY, strideX, strideT,
                                          padTop, padBottom, padLeft, padRight, padT) ;
      if (error != vl::vlSuccess) { return error ; }
    }

    /* compute derFilters dz/dF */
    if (derFilters) {
      // has derFilters, derOutput, data
      ptrdiff_t dataOffset = (data.getDimension(0)*data.getDimension(1)*data.getDimension(2)*data.getDimension(3)) * image ;
      error = vl::impl::vol2row<deviceType,type>(context,
                                          (type*)tempMemory,
                                          (type*)data.getMemory() + dataOffset,
                                          data.getDimension(0), data.getDimension(1), data.getDimension(2), data.getDimension(3),
                                          derFilters.getDimension(0), derFilters.getDimension(1), derFilters.getDimension(2),
                                          strideY, strideX, strideT,
                                          padTop, padBottom, padLeft, padRight, padT) ;
      if (error != vl::vlSuccess) { return error ; }
      for (int g = 0 ; g < numGroups ; ++ g) {
        ptrdiff_t filterGrpOffset = filtersVolume * numFiltersPerGroup * g ;
        ptrdiff_t tempGrpOffset = numOutputPixels * filtersVolume * g ;
        ptrdiff_t derOutputGrpOffset = numOutputPixels * numFiltersPerGroup * g  ;
        /* dzdF = temp' * dzdY */
        type alpha = 1 ;
        type beta = (image > 0) ; /* this saves init. the output array with 0 */
        error = vl::impl::blas<deviceType,dataType>::gemm(context,
                                't', 'n',
                                filtersVolume, numFiltersPerGroup, numOutputPixels,
                                alpha,
                                tempMemory + tempGrpOffset, numOutputPixels,
                                (type*)derOutput.getMemory() + derOutputOffset + derOutputGrpOffset, numOutputPixels,
                                beta,
                                (type*)derFilters.getMemory() + filterGrpOffset, filtersVolume) ;
        if (error != vl::vlSuccess) { return error ; }
      }
    }
  }

done:
  return context.passError(error, "nnconv3d_backward_blas<>: ") ;
}

#endif /* defined(__vl__nnconv3d_blas__) */
