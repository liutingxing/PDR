#include <math.h>
#include <memory.h>
#include "drFusion.h"

#define     STATE_NUM               (4)
#define     UD_NUM                  (STATE_NUM*(STATE_NUM+1)/2)
#define     MEAS_GNSS_NUM           (3)

#define     SIG_LAT                 (0.1)                       /* rms of pitch and latitude range (m) */
#define     SIG_LON                 (0.1)                       /* rms of pitch and longitude range (m) */
#define     SIG_HEADING             (10.0*PI/180)               /* rms of pitch and heading (rad) */
#define     SIG_LENGTH              (0.1)                       /* rms of step length (m) */

#define     LENGTH_TIME_CONSTANT    (100.0)
#define     SIGMA_LAT               (0.1 * 0.1)
#define     SIGMA_LON               (0.1 * 0.1)
#define     SIGMA_HEADING           (5.0*PI/180 * 5.0*PI/180)
#define     SIGMA_LENGTH            (0.1 * 0.1)

static const DBL INIT_RMS[] = {SIG_LAT, SIG_LON, SIG_HEADING, SIG_LENGTH};

static FLT dtCalculate(U32 timeNow, U32 timeLast);
static void setPhimQd(U32 utime, kalmanInfo_t* const pKalmanInfo, const drFusionData_t* const pFusionData);
static U32 gnssMeasUpdate(kalmanInfo_t* const pKalmanInfo, const drFusionData_t* const pFusionData);
static drFusionStatus_t errCorrection(kalmanInfo_t* const pKalmanInfo, drFusionData_t* const pFusionData);

/*-------------------------------------------------------------------------*/
/**
  @brief    
  @param    
  @return   
  

 */
/*--------------------------------------------------------------------------*/
static FLT dtCalculate(U32 timeNow, U32 timeLast)
{
    if (timeLast == 0)
    {
        return 1.0;
    }
    else if (timeLast > timeNow)
    {
        return (0xFFFFFFFF - timeLast + timeNow) / 1000.0F;
    }
    else
    {
        return (timeNow - timeLast) / 1000.0F;
    }
}

/*-------------------------------------------------------------------------*/
/**
  @brief    
  @param    
  @return   
  

 */
/*--------------------------------------------------------------------------*/
U32 drKalmanInit(kalmanInfo_t* const pKalmanInfo)
{
    U8 i;

    kalmanInit(pKalmanInfo, STATE_NUM);
    for (i = 0; i < STATE_NUM; i++)
    {
        pKalmanInfo->D_plus[i+1] = INIT_RMS[i] * INIT_RMS[i];
    }

    return 0;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    
  @param    
  @return   
  

 */
/*--------------------------------------------------------------------------*/
drFusionStatus_t drKalmanExec(U32 utime, kalmanInfo_t* const pKalmanInfo, drFusionData_t* const pFusionData)
{
    drFusionStatus_t retvel;

    setPhimQd(utime, pKalmanInfo, pFusionData);
    udKfPredict(pKalmanInfo);
    gnssMeasUpdate(pKalmanInfo, pFusionData);
    retvel = errCorrection(pKalmanInfo, pFusionData);

    return retvel;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    
  @param    
  @return   
  

 */
/*--------------------------------------------------------------------------*/
static void setPhimQd(U32 utime, kalmanInfo_t* const pKalmanInfo, const drFusionData_t* const pFusionData)
{
#if 1
    U32 i = 0;
    U32 j = 0;
    U32 stateNum = STATE_NUM;
    DBL phim[STATE_NUM][STATE_NUM] = {0};
    DBL qdt[STATE_NUM][STATE_NUM] = {0};
    DBL G[STATE_NUM][STATE_NUM] = {0};         // the row and col of shaping matrix are related with model rather than fixed.
    DBL GT[STATE_NUM][STATE_NUM] = {0};        // the transpose of G matrix
    DBL M2[STATE_NUM][STATE_NUM] = {0};
    DBL temp[STATE_NUM][STATE_NUM] = {0};
    DBL *pRowA[STATE_NUM] = {0};
    DBL *pRowB[STATE_NUM] = {0};
    DBL *pRowC[STATE_NUM] = {0};
    FLT fdt = 0.0F;

    for (i = 0; i <stateNum; i++)
    {
        for (j = 0; j < stateNum; j++)
        {
            phim[i][j] = 0.0F;
            qdt[i][j] = 0.0F;
        }
    }

    //set PHI matrix
    phim[0][3] =  pFusionData->fPdrFrequency * cosf(pFusionData->fPdrHeading);
    //phim[0][2] = -pFusionData->fPdrFrequency * pFusionData->fPdrStepLength * sinf(pFusionData->fPdrHeading);

    phim[1][3] =  pFusionData->fPdrFrequency * sinf(pFusionData->fPdrHeading);
    //phim[1][2] =  pFusionData->fPdrFrequency * pFusionData->fPdrStepLength * cosf(pFusionData->fPdrHeading);

    phim[3][3] = -1.0 / LENGTH_TIME_CONSTANT;

    //set Q matrix
    qdt[0][0] = SIGMA_LAT;
    qdt[1][1] = SIGMA_LON;
    qdt[2][2] = SIGMA_HEADING;
    qdt[3][3] = SIGMA_LENGTH;

    // set G matrix
    G[0][0] = 1.0;
    G[1][1] = 1.0;
    G[2][2] = 1.0;
    G[3][3] = 1.0;

    // GT = G'
    for (i = 0; i < stateNum; i++)
    {
        for (j = 0; j < stateNum; j++)
        {
            GT[i][j] = G[j][i];
        }
    }

    // qdt = G*w*G'
    for (i = 0; i < stateNum; i++)
    {
        pRowA[i] = G[i];
        pRowB[i] = qdt[i];
        pRowC[i] = temp[i];
    }
    matrixMult(pRowA, pRowB, stateNum, stateNum, stateNum, stateNum, pRowC);
    for (i = 0; i < stateNum; i++)
    {
        pRowA[i] = GT[i];
    }
    matrixMult(pRowC, pRowA, stateNum, stateNum, stateNum, stateNum, pRowB);

    // Q matrix discretization-2 order
    // M2=phi×M1，M1＝Q
    for (i = 0; i < stateNum; i++)
    {
        pRowA[i] = phim[i];
        pRowB[i] = qdt[i];
        pRowC[i] = M2[i];
    }
    matrixMult(pRowA, pRowB, stateNum, stateNum, stateNum, stateNum, pRowC);

    fdt = dtCalculate(utime, pFusionData->utime);
    for (i = 0; i < stateNum; i++)
    {
        for (j = 0; j < stateNum; j++)

        {
            qdt[i][j] = qdt[i][j] * fdt + (M2[i][j] + M2[j][i]) * fdt * fdt / 2.0;
        }
    }

    for (i = 0; i < stateNum; i++)
    {
        for (j = 0; j < stateNum; j++)
        {
            pKalmanInfo->Q[i][j] = qdt[i][j];
        }
    }

    // phi matrix discretization-2 order
    for (i = 0; i < stateNum; i++)
    {
        pRowA[i] = phim[i];
        pRowB[i] = temp[i];
    }
    matrixMult(pRowA, pRowA, stateNum, stateNum, stateNum, stateNum, pRowB);

    for (i = 0; i < stateNum; i++)
    {
        for (j = 0; j < stateNum; j++)
        {
            phim[i][j] = phim[i][j] * fdt + temp[i][j] * fdt * fdt / 2.0; //second order phi matrix

            if (j == i)
            {
                phim[i][j] += 1.0;
            }
        }
    }

    pKalmanInfo->msCnt = utime;
    pKalmanInfo->periodTms = (U16)(fdt*1000 + 0.5);

    for (i = 0; i < stateNum; i++)
    {
        for (j = 0; j < stateNum; j++)
        {
            if (j >= i)
            {
                pKalmanInfo->A[uMatIdx(i + 1, j + 1, stateNum)] = phim[i][j];
            }
        }
    }

#else
    pKalmanInfo->A[uMatIdx(1, 1, pKalmanInfo->stateNum)] = 1.0F;
    pKalmanInfo->A[uMatIdx(2, 2, pKalmanInfo->stateNum)] = 1.0F;
    pKalmanInfo->A[uMatIdx(3, 3, pKalmanInfo->stateNum)] = 1.0F;
    pKalmanInfo->Q[0][0] = SIGMA_LAT;
    pKalmanInfo->Q[1][1] = SIGMA_LON;
    pKalmanInfo->Q[2][2] = SIGMA_HEADING;
#endif
}

/*-------------------------------------------------------------------------*/
/**
  @brief    
  @param    
  @return   
  

 */
/*--------------------------------------------------------------------------*/
static U32 gnssMeasUpdate(kalmanInfo_t* const pKalmanInfo, const drFusionData_t* const pFusionData)
{
    U32 i = 0;
    U32 j = 0;
    DBL zc = 0.0;
    DBL rc = 0.0;
    DBL hc[STATE_NUM] = {0.0};
    DBL z[MEAS_GNSS_NUM] = {0.0};
    DBL h[MEAS_GNSS_NUM][STATE_NUM] = {0.0};
    DBL r[MEAS_GNSS_NUM] = {0.0};
    DBL test = 0.0;
    DBL deltaX[STATE_NUM] = {0.0};

    DBL gnssLatitude = pFusionData->fGnssLatitude;
    DBL gnssLongitude = pFusionData->fGnssLongitude;
    FLT gnssHeading = pFusionData->fGnssHeading;
    DBL pdrLatitude = pFusionData->fPdrLatitude;
    DBL pdrLongitude = pFusionData->fPdrLongitude;
    FLT pdrHeading = pFusionData->fPdrHeading;

    z[0] = (gnssLatitude - pdrLatitude) * RM(pdrLatitude);
    z[1] = (gnssLongitude - pdrLongitude) * RN(pdrLongitude);

    if ((gnssHeading - pdrHeading) > PI)
    {
        gnssHeading -= 2*PI;
    }
    if ((gnssHeading - pdrHeading) < -PI)
    {
        gnssHeading += 2*PI;
    }
    z[2] = gnssHeading - pdrHeading;

    h[0][0] = 1.0F;
    h[1][1] = 1.0F;
    h[2][2] = 1.0F;

    r[0] = 20 * 20;
    r[1] = 20 * 20;
    r[2] = 10 * DEG2RAD * 10 * DEG2RAD;

    for (i = 0; i < MEAS_GNSS_NUM; i++)
    {
        zc = z[i];
        rc = r[i];

        for (j = 0; j < STATE_NUM; j++)
        {
            hc[j] = h[i][j];
        }

        test = udKFUpdate(pKalmanInfo, hc, deltaX, rc, zc, 5, UPDATE_SAVE);
    }

    for (i = 0; i < STATE_NUM; i++)
    {
        pKalmanInfo->X[i] += deltaX[i];
    }

    return 0;
}

/*-------------------------------------------------------------------------*/
/**
  @brief    
  @param    
  @return   
  

 */
/*--------------------------------------------------------------------------*/
static drFusionStatus_t errCorrection(kalmanInfo_t* const pKalmanInfo, drFusionData_t* const pFusionData)
{
    drFusionStatus_t retvel = NoFix;

    if (fabs(pKalmanInfo->X[0]) < 10 && fabs(pKalmanInfo->X[1]) < 10)
    {
        pFusionData->fPdrLatitude = pFusionData->fPdrLatitude + pKalmanInfo->X[0]/RM(pFusionData->fGnssLatitude);
        pFusionData->fPdrLongitude = pFusionData->fPdrLongitude + pKalmanInfo->X[1]/RN(pFusionData->fGnssLatitude);
        pKalmanInfo->X[0] = 0.0F;
        pKalmanInfo->X[1] = 0.0F;
        retvel |= PosFix;
    }

    pFusionData->fPdrStepLength += (FLT)pKalmanInfo->X[3];
    pKalmanInfo->X[3] = 0.0;
    retvel |= LengthFix;

    if (fabs(pKalmanInfo->X[2]) < 10*DEG2RAD)
    {
        pFusionData->fPdrHeading = (FLT)(pFusionData->fPdrHeading + pKalmanInfo->X[2]);

        if (pFusionData->fPdrHeading > PI)
        {
            pFusionData->fPdrHeading -= 2*PI;
        }
        if (pFusionData->fPdrHeading < -PI)
        {
            pFusionData->fPdrHeading += 2*PI;
        }
        pKalmanInfo->X[2] = 0.0;
        retvel |= HeadingFix;
    }

    return retvel;
}