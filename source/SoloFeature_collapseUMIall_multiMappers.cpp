#include "SoloFeature.h"
#include "streamFuns.h"
#include "TimeFunctions.h"
#include "serviceFuns.cpp"
#include <unordered_map>
#include "SoloCommon.h"

inline int funCompareSolo1 (const void *a, const void *b);          //defined below
inline int funCompare_uint32_1_2_0 (const void *a, const void *b);

void SoloFeature::collapseUMIall(uint32 iCB, uint32 *umiArray) 
{
                                 
    uint32 *rGU=rCBp[iCB];
    uint32 rN=nReadPerCB[iCB]; 
    
    qsort(rGU,rN,rguStride*sizeof(uint32),funCompareNumbers<uint32>); //sort by gene index

    uint32 gid1=-1;//current gID
    uint32 nGenes=0, nGenesMult=0; //number of genes
    uint32 *gID = new uint32[min(featuresNumber,rN)+1]; //gene IDs
    uint32 *gReadS = new uint32[min(featuresNumber,rN)+1]; //start of gene reads TODO: allocate this array in the 2nd half of rGU
    for (uint32 iR=0; iR<rN*rguStride; iR+=rguStride) {
        if (rGU[iR+rguG]!=gid1) {//record gene boundary
            gReadS[nGenes]=iR;
            gid1=rGU[iR+rguG];
            gID[nGenes]=gid1;
            
            ++nGenes;            
            if (gid1>=geneMultMark)
                ++nGenesMult;
        };
    };
    gReadS[nGenes]=rguStride*rN;//so that gReadS[nGenes]-gReadS[nGenes-1] is the number of reads for nGenes, see below in qsort
    nGenes -= nGenesMult;//unique only gene
   
    unordered_map <uintUMI, unordered_map<uint32,uint32>> umiGeneHash, umiGeneHash0;
                   //UMI                 //Gene //Count
    if (pSolo.umiFiltering.MultiGeneUMI) {
        for (uint32 iR=0; iR<gReadS[nGenes]; iR+=rguStride) {
            umiGeneHash[rGU[iR+1]][rGU[iR]]++; 
        };

        for (auto &iu : umiGeneHash) {//loop over all UMIs
            if (iu.second.size()==1)
                continue;
            uint32 maxu=0;
            for (const auto &ig : iu.second) {//loop over genes for a given UMI
                if (maxu<ig.second)
                    maxu=ig.second; //find gene with maximum count
            };
            if (maxu==1)
                maxu=2;//to kill UMIs with 1 read to one gene, 1 read to another gene
            for (auto &ig : iu.second) {
                if (maxu>ig.second)
                    ig.second=0; //kills Gene with read count *strictly* < maximum count
            };
        };
    };
    
    vector<unordered_map <uintUMI,uintUMI>> umiCorrected(nGenes);

    if (countCellGeneUMI.size() < countCellGeneUMIindex[iCB] + nGenes*countMatStride)
        countCellGeneUMI.resize((countCellGeneUMI.size() + nGenes*countMatStride )*2);//allocated vector too small
    
    nGenePerCB[iCB]=0;
    nUMIperCB[iCB]=0;
    countCellGeneUMIindex[iCB+1]=countCellGeneUMIindex[iCB];
    /////////////////////////////////////////////
    /////////// main cycle over genes
    for (uint32 iG=0; iG<nGenes; iG++) {//collapse UMIs for each gene
        uint32 *rGU1=rGU+gReadS[iG];
            
        uint32 nR0 = (gReadS[iG+1]-gReadS[iG])/rguStride; //total number of reads
        if (nR0==0)
            continue; //no reads - this should not happen?
            
        qsort(rGU1, nR0, rguStride*sizeof(uint32), funCompareTypeShift<uint32,rguU>);
            
        //exact collapse
        uint32 iR1=-umiArrayStride; //number of distinct UMIs for this gene
        uint32 u1=-1;
        for (uint32 iR=rguU; iR<gReadS[iG+1]-gReadS[iG]; iR+=rguStride) {//count and collapse identical UMIs
            if (pSolo.umiFiltering.MultiGeneUMI && umiGeneHash[rGU1[iR]][gID[iG]]==0) {//multigene UMI is not recorded
                if ( pSolo.umiDedup.typeMain != UMIdedup::typeI::NoDedup ) //for NoDedup, the UMI filtering is not done
                    rGU1[iR] = (uintUMI) -1; //mark multigene UMI, so that UB tag will be set to -
                continue;
            };            
                
            if (rGU1[iR]!=u1) {
                iR1 += umiArrayStride;
                u1=rGU1[iR];
                umiArray[iR1]=u1;
                umiArray[iR1+1]=0;
            };
            umiArray[iR1+1]++;
            //if ( umiArray[iR1+1]>nRumiMax) nRumiMax=umiArray[iR1+1];
        };

        uint32 nU0 = (iR1+umiArrayStride)/umiArrayStride;//number of UMIs after simple exact collapse
       
        if (pSolo.umiFiltering.MultiGeneUMI_CR) {
            if (nU0==0)
                continue; //nothing to count
                
            for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
                umiGeneHash0[umiArray[iu+0]][iG]+=umiArray[iu+1];//this sums read counts over UMIs that were collapsed
            };
                
            umiArrayCorrect_CR(nU0, umiArray, readInfo.size()>0, false, umiCorrected[iG]);
                
            for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {//just fill the umiGeneHash - will calculate UMI counts later
                umiGeneHash[umiArray[iu+2]][iG]+=umiArray[iu+1];//this sums read counts over UMIs that were collapsed
            };
                
            continue; //done with MultiGeneUMI_CR, readInfo will be filled later
        };        
            
            
        if (pSolo.umiDedup.yes.NoDedup)
            countCellGeneUMI[countCellGeneUMIindex[iCB+1] + pSolo.umiDedup.countInd.NoDedup] = nR0;

        if (nU0>0) {//otherwise no need to count
            if (pSolo.umiDedup.yes.Exact)
                countCellGeneUMI[countCellGeneUMIindex[iCB+1] + pSolo.umiDedup.countInd.Exact] = nU0;
                
            if (pSolo.umiDedup.yes.CR)
                countCellGeneUMI[countCellGeneUMIindex[iCB+1] + pSolo.umiDedup.countInd.CR] = 
                    umiArrayCorrect_CR(nU0, umiArray, readInfo.size()>0 && pSolo.umiDedup.typeMain==UMIdedup::typeI::CR, true, umiCorrected[iG]);
                
            if (pSolo.umiDedup.yes.Directional)
                countCellGeneUMI[countCellGeneUMIindex[iCB+1] + pSolo.umiDedup.countInd.Directional] = 
                    umiArrayCorrect_Directional(nU0, umiArray, readInfo.size()>0 && pSolo.umiDedup.typeMain==UMIdedup::typeI::Directional, true, umiCorrected[iG], 0);
                    
            if (pSolo.umiDedup.yes.Directional_UMItools)
                countCellGeneUMI[countCellGeneUMIindex[iCB+1] + pSolo.umiDedup.countInd.Directional_UMItools] = 
                    umiArrayCorrect_Directional(nU0, umiArray, readInfo.size()>0 && pSolo.umiDedup.typeMain==UMIdedup::typeI::Directional_UMItools, true, umiCorrected[iG], -1);                    
                
            //this changes umiArray, so it should be last call
            if (pSolo.umiDedup.yes.All)
                countCellGeneUMI[countCellGeneUMIindex[iCB+1] + pSolo.umiDedup.countInd.All] = 
                    umiArrayCorrect_Graph(nU0, umiArray, readInfo.size()>0 && pSolo.umiDedup.typeMain==UMIdedup::typeI::All, true, umiCorrected[iG]);
        };//if (nU0>0)
        
        {//check any count>0 and finalize record for this gene
            uint32 totcount=0;
            for (uint32 ii=countCellGeneUMIindex[iCB+1]+1; ii<countCellGeneUMIindex[iCB+1]+countMatStride; ii++) {
                totcount += countCellGeneUMI[ii];
            };
            if (totcount>0) {//at least one umiDedup type is non-0
                countCellGeneUMI[countCellGeneUMIindex[iCB+1] + 0] = gID[iG];
                nGenePerCB[iCB]++;
                nUMIperCB[iCB] += countCellGeneUMI[countCellGeneUMIindex[iCB+1] + pSolo.umiDedup.countInd.main];
                countCellGeneUMIindex[iCB+1] = countCellGeneUMIindex[iCB+1] + countMatStride;//iCB+1 accumulates the index
            };
        };        
        
        if (readInfo.size()>0) {//record cb/umi for each read
            for (uint32 iR=0; iR<gReadS[iG+1]-gReadS[iG]; iR+=rguStride) {//cycle over reads
                uint64 iread1 = rGU1[iR+rguR];
                readInfo[iread1].cb = indCB[iCB] ;
                uint32 umi=rGU1[iR+rguU];
                
                if (umiCorrected[iG].count(umi)>0)
                    umi=umiCorrected[iG][umi]; //correct UMI
                readInfo[iread1].umi=umi;
            };      
        };            
    };

    if (pSolo.umiFiltering.MultiGeneUMI_CR) {
        
        vector<uint32> geneCounts(nGenes,0);
        
        vector<unordered_set<uintUMI>> geneUmiHash;
        if (readInfo.size()>0)
            geneUmiHash.resize(nGenes);
        
        for (const auto &iu: umiGeneHash) {//loop over UMIs for all genes
                       
            uint32 maxu=0, maxg=-1;
            for (const auto &ig : iu.second) {
                if (ig.second>maxu) {
                    maxu=ig.second;
                    maxg=ig.first;
                } else if (ig.second==maxu) {
                    maxg=-1;
                };
            };

            if ( maxg+1==0 )
                continue; //this umi is not counted for any gene, because two genes have the same read count for this UMI
            
            for (const auto &ig : umiGeneHash0[iu.first]) {//check that this umi/gene had also top count for uncorrected umis
                if (ig.second>umiGeneHash0[iu.first][maxg]) {
                    maxg=-1;
                    break;
                };
            };

            if ( maxg+1!=0 ) {//this UMI is counted
                geneCounts[maxg]++;
                if (readInfo.size()>0)
                    geneUmiHash[maxg].insert(iu.first);
            };
        };

        for (uint32 ig=0; ig<nGenes; ig++) {
            if (geneCounts[ig] == 0)
                continue; //no counts for this gene
            nGenePerCB[iCB]++;
            nUMIperCB[iCB] += geneCounts[ig];
            countCellGeneUMI[countCellGeneUMIindex[iCB+1] + 0] = gID[ig];
            countCellGeneUMI[countCellGeneUMIindex[iCB+1] + pSolo.umiDedup.countInd.CR] = geneCounts[ig];
            countCellGeneUMIindex[iCB+1] = countCellGeneUMIindex[iCB+1] + countMatStride;//iCB+1 accumulates the index
        };
        
        if (readInfo.size()>0) {//record cb/umi for each read
            for (uint32 iG=0; iG<nGenes; iG++) {//cycle over genes
                uint32 *rGU1=rGU+gReadS[iG];            
                for (uint32 iR=0; iR<gReadS[iG+1]-gReadS[iG]; iR+=rguStride) {//cycle over reads
                    uint64 iread1 = rGU1[iR+rguR];
                    readInfo[iread1].cb = indCB[iCB] ;
                    uint32 umi=rGU1[iR+rguU];
                    
                    if (umiCorrected[iG].count(umi)>0)
                        umi=umiCorrected[iG][umi]; //correct UMI

                    //cout << iG << "-" << iR << " " <<flush ;
                    if (geneUmiHash[iG].count(umi)>0) {
                        readInfo[iread1].umi=umi;
                    } else {
                        readInfo[iread1].umi=(uintUMI) -1;
                    };
                };
            };
        };
    };
    
    if (nGenesMult>0) {//process multigene reads
               
        std::vector<vector<uint32>> umiGenes;
        umiGenes.reserve(256);
        {//for each umi, count number of reads per gene. 
         //Output umiGenes: only genes with nReads = nReads-for-this-UMI will be kept for this UMI
            uint32 *rGUm = rGU + gReadS[nGenes];
            uint32 nRm=( gReadS[nGenes+nGenesMult] - gReadS[nGenes] ) / rguStride;
            
            //sort by UMI, then by read, then by gene
            qsort(rGUm, nRm, rguStride*sizeof(uint32), funCompare_uint32_1_2_0);//there is no need to sort by read or gene actually
            
            std::unordered_map<uint32, uint32> geneReadCount; //number of reads per gene
            uint32 nRumi=0;
            bool skipUMI=false;
            uintUMI umiPrev = (uintUMI)-1;
            uintRead readPrev = (uintRead)-1;
            for (uint32 iR=0; iR<nRm*rguStride; iR+=rguStride) {//for each umi, find intersection of genes from each read
                uintUMI umi1 = rGUm[iR+1];
                if (umi1!=umiPrev) {//starting new UMI
                    umiPrev = umi1;                
                    if (umiGeneHash.count(umi1)>0) {
                        skipUMI = true;//this UMI is skipped because it was among uniquely mapped
                    } else {
                        skipUMI = false;//new good umi
                        geneReadCount.clear();
                        nRumi=0;
                        readPrev = (uintRead)-1;
                    };
                };
                
                if (skipUMI)
                    continue; //this UMI is skipped because it was among uniquely mapped
                
                uintRead read1 = rGUm[iR+2];
                if (read1 != readPrev) {
                    ++nRumi;
                    readPrev = read1;
                };
                
                uint32 g1 = rGUm[iR+0] ^ geneMultMark; //XOR to unset the geneMultMark bit
                geneReadCount[g1]++;
                
                if (iR == nRm*rguStride-rguStride || umi1 != rGUm[iR+1+rguStride]) {//record this umi
                    uint32 ng=0;
                    for (const auto &gg: geneReadCount) {
                        if (gg.second == nRumi)
                            ++ng;
                    };
                    vector<uint32> vg;
                    vg.reserve(ng);//this and above is to construct vector of precise size, for efficiency?
                    for (const auto &gg: geneReadCount) {
                        if (gg.second == nRumi)
                            vg.push_back(gg.first);
                    };                
                    umiGenes.push_back(vg);
                };
            };
        };
        
        std::map<uint32,uint32> genesM; //genes to quantify
        
        {//collect all genes, replace geneID with index in umiGenes
            uint32 ng = 0;
            for (auto &uu: umiGenes) {
                for (auto &gg: uu) {
                    if (genesM.count(gg) == 0) {//new gene
                        genesM[gg]=ng;
                        ++ng;
                    };
                    gg = genesM[gg];
                };
            };
        };
        
       
        vector<double> gE1(genesM.size(), 0);
        {//gE1=uniformly distribute multigene UMIs
            for (auto &ug: umiGenes) {
                for (auto &gg: ug) {
                    gE1[gg] += 1.0 / double(ug.size()); // 1/n_genes_umi
                };
            };
        };

        vector<vector<double>> gE2(pSolo.umiDedup.yes.N);
        if (pSolo.multiMap.yes.Rescue) {
            
            for (uint32 indDedup=0; indDedup < pSolo.umiDedup.yes.N; indDedup++) {
                vector<double> gEu(genesM.size(), 0);
                {//collect unique gene counts
                    for (uint32 igm=countCellGeneUMIindex[iCB]; igm<countCellGeneUMIindex[iCB+1]; igm+=countMatStride) {
                        uint32 g1 = countCellGeneUMI[igm];
                        if (genesM.count(g1)>0)
                            gEu[genesM[g1]]=(double)countCellGeneUMI[igm+1+indDedup];
                    };
                };
                
                gE2[indDedup].resize(genesM.size(), 0);
                {//gE2=distribute UMI proportionally to gE1+gEu
                    for (auto &ug: umiGenes) {
                        double norm1 = 0.0;
                        for (auto &gg: ug)
                            norm1 += gE1[gg]+gEu[gg];
                        
                        if (norm1==0.0)
                            continue; //this should not happen since gE1 is non-zero for all genes involved
                        norm1 = 1.0 / norm1;
                        
                        for (auto &gg: ug) {
                            gE2[indDedup][gg] += (gE1[gg]+gEu[gg])*norm1;
                        };
                    };
                };
            };
        };
        
        {//write to countMatMult
            uint32 ig=0;
            for (const auto &gm: genesM) {
                countMatMult.m[countMatMult.i[iCB+1] + 0] = gm.first;
                countMatMult.m[countMatMult.i[iCB+1] + 1] = gE1[ig];
                if (pSolo.multiMap.yes.Rescue) {
                    for (uint32 indDedup=0; indDedup < pSolo.umiDedup.yes.N; indDedup++) {
                        countMatMult.m[countMatMult.i[iCB+1] + 2 + indDedup] = gE2[indDedup][ig];
                    };
                };
                ++ig;
                countMatMult.i[iCB+1] += countMatMult.s;
            };
        };
            
        /*
        //record multi
        countMatMult.i[iCB]; igm<countCellGeneUMIindex[iCB+1]; igm+=countMatStride) {
            uint32 g1 = countCellGeneUMI[igm];
            if (genesM.count(g1)>0) {
                if (pSolo.multiMap.yes.Rescue) {           
                    for (uint32 indDedup=0; indDedup < pSolo.umiDedup.yes.N; indDedup++)
                        countCellGeneUMI[igm+1+pSolo.umiDedup.yes.N+indDedup] = gE2[indDedup][g1];
                };
            };
        };        
        */
        
        int a=1;
    };
};

////////////////////////////////////////////////////////////////////////////// sorting functions
inline int funCompareSolo1 (const void *a, const void *b) {
    uint32 *va= (uint32*) a;
    uint32 *vb= (uint32*) b;

    if (va[1]>vb[1]) {
        return 1;
    } else if (va[1]<vb[1]) {
        return -1;
    } else if (va[0]>vb[0]){
        return 1;
    } else if (va[0]<vb[0]){
        return -1;
    } else {
        return 0;
    };
};

inline int funCompare_uint32_1_2_0 (const void *a, const void *b) {
    uint32 *va= (uint32*) a;
    uint32 *vb= (uint32*) b;

    if (va[1]>vb[1]) {
        return 1;
    } else if (va[1]<vb[1]) {
        return -1;
    } else if (va[2]>vb[2]){
        return 1;
    } else if (va[2]<vb[2]){
        return -1;
    } else if (va[0]>vb[0]){
        return 1;
    } else if (va[0]<vb[0]){
        return -1;
    } else {
        return 0;
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////
uint32 SoloFeature::umiArrayCorrect_CR(const uint32 nU0, uintUMI *umiArr, const bool readInfoRec, const bool nUMIyes, unordered_map <uintUMI,uintUMI> &umiCorr)
{
    qsort(umiArr, nU0, umiArrayStride*sizeof(uint32), funCompareSolo1);
    
    for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
        
        umiArr[iu+2] = umiArr[iu+0]; //stores corrected UMI for 1MM_CR and 1MM_Directional
        for (uint64 iuu=(nU0-1)*umiArrayStride; iuu>iu; iuu-=umiArrayStride) {

            uint32 uuXor = umiArr[iu+0] ^ umiArr[iuu+0];

            if ( (uuXor >> (__builtin_ctz(uuXor)/2)*2) <= 3 ) {//1MM                 
                umiArr[iu+2]=umiArr[iuu+0];//replace iu with iuu
                break;
            };
        };
    };
    
    if (readInfoRec) {//record corrections
        for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
            if (umiArr[iu+0] != umiArr[iu+2])
                umiCorr[umiArr[iu+0]]=umiArr[iu+2];
        };
    };
    
    if (!nUMIyes) {
        return 0;
    } else {
        unordered_set<uintUMI> umiC;
        for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
            umiC.insert(umiArr[iu+2]);
        };
       return umiC.size();
    };
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////
uint32 SoloFeature::umiArrayCorrect_Directional(const uint32 nU0, uintUMI *umiArr, const bool readInfoRec, const bool nUMIyes, unordered_map <uintUMI,uintUMI> &umiCorr, const int32 dirCountAdd)
{
    qsort(umiArr, nU0, umiArrayStride*sizeof(uint32), funCompareNumbersReverseShift<uint32, 1>);//TODO no need to sort by sequence here, only by count. 
    
    for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride)
        umiArr[iu+2] = umiArr[iu+0]; //initialized - it will store corrected UMI for 1MM_CR and 1MM_Directional

    uint32 nU1 = nU0;
    for (uint64 iu=umiArrayStride; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
        
        for (uint64 iuu=0; iuu<iu; iuu+=umiArrayStride) {

            uint32 uuXor = umiArr[iu+0] ^ umiArr[iuu+0];

            if ( (uuXor >> (__builtin_ctz(uuXor)/2)*2) <= 3 && umiArr[iuu+1] >= (2*umiArr[iu+1]+dirCountAdd) ) {//1MM && directional condition
                umiArr[iu+2]=umiArr[iuu+2];//replace iuu with iu-corrected
                nU1--;
                break;
            };
        };
    };
    
    if (readInfoRec) {//record corrections
        for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
            if (umiArr[iu+0] != umiArr[iu+2])
                umiCorr[umiArr[iu+0]]=umiArr[iu+2];
        };
    };
    
    if (!nUMIyes) {
        return 0;
    } else {
        unordered_set<uintUMI> umiC;
        for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
            umiC.insert(umiArr[iu+2]);
        };
        if (umiC.size()!=nU1)
            cout << nU1 <<" "<< umiC.size()<<endl;
        return umiC.size();
    };
};

