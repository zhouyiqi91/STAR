#include "SoloFeature.h"
#include "streamFuns.h"
#include "Stats.h"
#include "GlobalVariables.h"

void SoloFeature::statsOutput()
{
    ofstream &strOut=ofstrOpen(outputPrefix+pSolo.outFileNames[4], ERROR_OUT, P);
    //Sequencing
    strOut << "Number of Reads," << g_statsAll.readN <<'\n';
    strOut << "Reads With Valid Barcodes," << 1.0 - double( readBarSum->stats.numInvalidBarcodes() + readFeatSum->stats.numInvalidBarcodes() )/g_statsAll.readN <<'\n';
    strOut << "Sequencing Saturation," << readFeatSum->stats.numSequencingSaturation() <<'\n';
    
    //quality scores
    uint64 q30[2]={}, ntot[2]={};
    for (uint32 imate=0; imate<2; imate++) {
        for (uint32 ix=0; ix<256; ix++) {
            ntot[imate] += g_statsAll.qualHist[imate][ix];
            if (ix >= (P.readQualityScoreBase + 30))
                q30[imate] += g_statsAll.qualHist[imate][ix];
        };
    };
    strOut << "Q30 Bases in CB+UMI," << double(q30[1])/ntot[1] <<'\n';
    strOut << "Q30 Bases in RNA read," << double(q30[0])/ntot[0] <<'\n';

    
    strOut << "Reads Mapped to Genome: Unique+Multiple," << double(g_statsAll.mappedReadsU+g_statsAll.mappedReadsM)/g_statsAll.readN <<'\n';
    strOut << "Reads Mapped to Genome: Unique," << double(g_statsAll.mappedReadsU)/g_statsAll.readN <<'\n';
    
    strOut << "Reads Mapped to Transcriptome: Unique+Multipe Genes," << double( readFeatSum->stats.numMappedToTranscriptome() )/g_statsAll.readN <<'\n';
    strOut << "Reads Mapped to Transcriptome: Unique Genes," << double( readFeatSum->stats.numMappedToTranscriptomeUnique() )/g_statsAll.readN <<'\n';
    
    if (pSolo.cellFilter.type[0]=="CellRanger2.2") {
        strOut << "Estimated Number of Cells," << filteredCells.nCells <<'\n';
        
        strOut << "Reads in Cells Mapped to Unique Genes," << filteredCells.nReadInCells <<'\n';
        strOut << "Fraction of Reads in Cells," << double(filteredCells.nReadInCells) / readFeatSum->stats.numMappedToTranscriptomeUnique() <<'\n';
        strOut << "Mean Reads per Cell," << filteredCells.meanReadPerCell <<'\n';
        strOut << "Median Reads per Cell," << filteredCells.medianReadPerCell <<'\n';
                
        strOut << "UMIs in Cells," << filteredCells.nUMIinCells <<'\n';
        strOut << "Mean UMI per Cell," << filteredCells.meanUMIperCell <<'\n';
        strOut << "Median UMI per Cell," << filteredCells.medianUMIperCell <<'\n';    
        
        strOut << "Mean Genes per Cell," << filteredCells.meanGenePerCell <<'\n';
        strOut << "Median Genes per Cell," << filteredCells.medianGenePerCell <<'\n';    
        strOut << "Total Genes Detected," << filteredCells.nGeneDetected <<'\n';    
    };
    
    
    strOut.close();
};