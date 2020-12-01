#include "ReadAlignChunk.h"
#include "GlobalVariables.h"
#include "ThreadControl.h"
#include "ErrorWarning.h"
#include "SequenceFuns.h"
#include "GlobalVariables.h"

inline uint64 fastqReadOneLine(ifstream &streamIn, char *arrIn);
inline void removeStringEndControl(string &str);


void ReadAlignChunk::processChunks() {//read-map-write chunks
    noReadsLeft=false; //true if there no more reads left in the file
    bool newFile=false; //new file marker in the input stream
    while (!noReadsLeft) {//continue until the input EOF
            //////////////read a chunk from input files and store in memory
        if (P.outFilterBySJoutStage<2) {//read chunks from input file

            if (P.runThreadN>1) pthread_mutex_lock(&g_threadChunks.mutexInRead);

            chunkInSizeBytesTotal={0,0};
            
            while (chunkInSizeBytesTotal[0] < P.chunkInSizeBytes && chunkInSizeBytesTotal[1] < P.chunkInSizeBytes && P.inOut->readIn[0].good() && P.inOut->readIn[1].good()) {
                char nextChar=P.inOut->readIn[0].peek();
                if (P.iReadAll==P.readMapNumber) {//do not read any more reads
                    break;
                } else if (P.readFilesTypeN==10 && P.inOut->readIn[0].good() && P.outFilterBySJoutStage!=2) {//SAM input && not eof && not 2nd stage

                    string str1;

                    if (nextChar=='@') {//with SAM input linest that start with @ are headers
                        getline(P.inOut->readIn[0], str1); //read line and skip it
                        continue;
                    };

                    P.inOut->readIn[0] >> str1;
                    if (str1=="FILE") {
                        newFile=true;
                    } else {
                        P.iReadAll++; //increment read number

                        uint imate1=0;
                        for (uint imate=0;imate<P.readNmates;imate++) {
                            if (imate>0)
                                P.inOut->readIn[0] >> str1; //for imate=0 str1 was already read
                            uint flag;
                            P.inOut->readIn[0] >>flag; //read name and flag
                            char passFilterIllumina=(flag & 0x800 ? 'Y' : 'N');

                            if (imate==1) {//2nd line is always opposite of the 1st one
                                imate1=1-imate1;
                            } else if (P.readNmates==2 && (flag & 0x80)) {
                                imate1=1;
                            } else {
                                imate1=0;
                            };

                            //read ID or number
                            if (P.outSAMreadID=="Number") {
                                chunkInSizeBytesTotal[imate1] += sprintf(chunkIn[imate1] + chunkInSizeBytesTotal[imate1], "@%llu", P.iReadAll);
                            } else {
                                chunkInSizeBytesTotal[imate1] += sprintf(chunkIn[imate1] + chunkInSizeBytesTotal[imate1], "@%s", str1.c_str());
                            };

                            //iReadAll, passFilterIllumina, passFilterIllumina
                            chunkInSizeBytesTotal[imate1] += sprintf(chunkIn[imate1] + chunkInSizeBytesTotal[imate1], " %llu %c %i", P.iReadAll, passFilterIllumina, P.readFilesIndex);

                            for (int ii=3; ii<=9; ii++)
                                P.inOut->readIn[0] >> str1; //skip fields until sequence

                            string seq1,qual1;
                            P.inOut->readIn[0]  >> seq1 >> qual1;
                            if (flag & 0x10) {//sequence reverse-coomplemented
                                revComplementNucleotides(seq1);
                                reverse(qual1.begin(),qual1.end());
                            };

                            getline(P.inOut->readIn[0],str1); //str1 is now all SAM attributes
                            chunkInSizeBytesTotal[imate1] += sprintf(chunkIn[imate1] + chunkInSizeBytesTotal[imate1], "%s\n%s\n+\n%s\n", str1.c_str(), seq1.c_str(), qual1.c_str());
                        };
                    };
                } else if (nextChar=='@') {//fastq, not multi-line
                    P.iReadAll++; //increment read number
                    if (P.outFilterBySJoutStage!=2) {//not the 2nd stage of the 2-stage mapping, read ID from the 1st read
                        string readID;
                        P.inOut->readIn[0] >> readID;
                        removeStringEndControl(readID);
                        if (P.outSAMreadIDnumber) {
                            readID="@"+to_string(P.iReadAll);
                        };
                        //read the second field of the read name line
                        char passFilterIllumina='N';
                        if (P.inOut->readIn[0].peek()!='\n') {//2nd field exists
                            string field2;
                            P.inOut->readIn[0] >> field2;
                            if (field2.length()>=3 && field2[1]==':' && field2[2]=='Y' && field2[3]==':' )
                                passFilterIllumina='Y';
                        };
                        
                        //add extra information to readID line
                        readID += ' '+ to_string(P.iReadAll)+' '+passFilterIllumina+' '+to_string(P.readFilesIndex);

                        //ignore the rest of the read name for both mates
                        for (uint imate=0; imate<P.readNmatesIn; imate++)
                            P.inOut->readIn[imate].ignore(DEF_readNameSeqLengthMax,'\n');

                        if (P.pSolo.barcodeReadYes) {//record barcode sequence
                            string seq1;
                            getline(P.inOut->readIn[P.pSolo.barcodeRead],seq1);
                            removeStringEndControl(seq1);
                            if (seq1.size() != P.pSolo.bL) {
                                if (P.pSolo.bL > 0) {
                                    ostringstream errOut;
                                    errOut << "EXITING because of FATAL ERROR in input read file: the total length of barcode sequence is "  << seq1.size() << " not equal to expected " <<P.pSolo.bL <<"\n"  ;
                                    errOut << "Read ID="<<readID<< "   Sequence="<<seq1<<"\n";
                                    errOut << "SOLUTION: make sure that the barcode read is the last file in --readFilesIn , and check that it has the correct formatting\n";
                                    errOut << "          If UMI+CB length is not equal to the barcode read length, specify barcode read length with --soloBarcodeReadLength\n";
                                    exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
                                } else if (seq1.size()<P.pSolo.cbumiL) {//barcode sequence too short - append Ns
                                    seq1.append(P.pSolo.cbumiL-seq1.size(), 'N');
                                };
                            };
                            readID += ' ' + seq1;
                            P.inOut->readIn[P.pSolo.barcodeRead].ignore(DEF_readNameSeqLengthMax,'\n');//skip to the end of 3rd ("+") line
                            getline(P.inOut->readIn[P.pSolo.barcodeRead],seq1); //read qualities
                            removeStringEndControl(seq1);
                            readID += ' ' + seq1;
                            g_statsAll.qualHistCalc(1, seq1.c_str()+P.pSolo.barcodeStart, P.pSolo.barcodeEnd==0 ? seq1.size() : P.pSolo.barcodeEnd-P.pSolo.barcodeStart+1);
                        };

                        //copy the same readID to both mates
                        for (uint imate=0; imate<P.readNmates; imate++) {
                            chunkInSizeBytesTotal[imate] += 1 + readID.copy(chunkIn[imate] + chunkInSizeBytesTotal[imate], readID.size(),0);
                            chunkIn[imate][chunkInSizeBytesTotal[imate]-1]='\n';
                        };
                    };
                    //copy 3 (4 for stage 2) lines: sequence, dummy, quality
                    for (uint imate=0; imate<P.readNmates; imate++) {
                        // read 1st line for 2nd stage only
                        if (P.outFilterBySJoutStage == 2)
                            chunkInSizeBytesTotal[imate] += fastqReadOneLine(P.inOut->readIn[imate], chunkIn[imate] + chunkInSizeBytesTotal[imate]);
                        //sequence
                        chunkInSizeBytesTotal[imate] += fastqReadOneLine(P.inOut->readIn[imate], chunkIn[imate] + chunkInSizeBytesTotal[imate]);
                        //skip 3rd line, record '+'
                        P.inOut->readIn[imate].ignore(DEF_readNameSeqLengthMax, '\n');
                        chunkIn[imate][chunkInSizeBytesTotal[imate]] = '+';
                        chunkIn[imate][chunkInSizeBytesTotal[imate]+1] = '\n';
                        chunkInSizeBytesTotal[imate] += 2;
                        //quality
                        uint64 lenIn = fastqReadOneLine(P.inOut->readIn[imate], chunkIn[imate] + chunkInSizeBytesTotal[imate]);
                        if (P.outFilterBySJoutStage != 2) {
                                g_statsAll.qualHistCalc(imate, chunkIn[imate] + chunkInSizeBytesTotal[imate], lenIn);
                        };
                        chunkInSizeBytesTotal[imate] += lenIn;
                    };
                } else if (nextChar=='>') {//fasta, can be multiline, which is converted to single line
                    P.iReadAll++; //increment read number
                    for (uint imate=0; imate<P.readNmatesIn; imate++) {
                        if (P.outFilterBySJoutStage!=2) {//not the 2nd stage of the 2-stage mapping

                            if (P.outSAMreadID=="Number") {
                                chunkInSizeBytesTotal[imate] += sprintf(chunkIn[imate] + chunkInSizeBytesTotal[imate], ">%llu", P.iReadAll);
                            } else {
                                P.inOut->readIn[imate] >> (chunkIn[imate] + chunkInSizeBytesTotal[imate]);
                                chunkInSizeBytesTotal[imate] += strlen(chunkIn[imate] + chunkInSizeBytesTotal[imate]);
                            };

                            P.inOut->readIn[imate].ignore(DEF_readNameSeqLengthMax,'\n');

                            chunkInSizeBytesTotal[imate] += sprintf(chunkIn[imate] + chunkInSizeBytesTotal[imate], " %llu %c %i \n", P.iReadAll, 'N', P.readFilesIndex);
                        };
                        
                        //read multi-line fasta
                        nextChar=P.inOut->readIn[imate].peek();
                        while (nextChar!='@' && nextChar!='>' && nextChar!=' ' && nextChar!='\n' && P.inOut->readIn[imate].good()) {
                            P.inOut->readIn[imate].getline(chunkIn[imate] + chunkInSizeBytesTotal[imate], DEF_readSeqLengthMax + 1 );
                            if (P.inOut->readIn[imate].gcount()<2) 
                                break; //no more input
                                
                            chunkInSizeBytesTotal[imate] += P.inOut->readIn[imate].gcount()-1; //-1 because \n was counted, bu wee need to remove it
                            if ( int(chunkIn[imate][chunkInSizeBytesTotal[imate]-1]) < 33 ) {//remove control char at the end if present
                                chunkInSizeBytesTotal[imate]--;
                            };
                            
                            nextChar=P.inOut->readIn[imate].peek();
                        };
                        chunkIn[imate][chunkInSizeBytesTotal[imate]]='\n';
                        chunkInSizeBytesTotal[imate] ++;
                    };
                } else if (nextChar==' ' || nextChar=='\n' || !P.inOut->readIn[0].good()) {//end of stream
                    P.inOut->logMain << "Thread #" <<iThread <<" end of input stream, nextChar="<<int(nextChar) <<endl;
                    break;
                } else {
                    string word1;
                    P.inOut->readIn[0] >> word1;
                    if (word1=="FILE") {//new file marker
                        newFile=true;
                    } else {//error
                        ostringstream errOut;
                        errOut << ERROR_OUT <<" EXITING because of FATAL ERROR in input reads: unknown file format: the read ID should start with @ or > \n";
                        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
                    };
                };

                if (newFile) {
                        P.inOut->readIn[0] >> P.readFilesIndex;
                        pthread_mutex_lock(&g_threadChunks.mutexLogMain);
                        P.inOut->logMain << "Starting to map file # " << P.readFilesIndex<<"\n";
                        for (uint imate=0; imate<P.readFilesNames.size(); imate++) {
                            P.inOut->logMain << "mate " <<imate+1 <<":   "<<P.readFilesNames.at(imate).at(P.readFilesIndex) <<"\n";
                            P.inOut->readIn[imate].ignore(numeric_limits<streamsize>::max(),'\n');
                        };
                        P.inOut->logMain<<flush;
                        pthread_mutex_unlock(&g_threadChunks.mutexLogMain);
                        newFile=false;
                };
            };
            //TODO: check here that both mates are zero or non-zero
            if (chunkInSizeBytesTotal[0]==0) {
                noReadsLeft=true; //true if there no more reads left in the file
                iChunkIn=g_threadChunks.chunkInN;//to keep things consistent
                g_threadChunks.chunkInN++;
            } else {
                noReadsLeft=false;
                iChunkIn=g_threadChunks.chunkInN;
                g_threadChunks.chunkInN++;
            };

            for (uint imate=0; imate<P.readNmates; imate++) chunkIn[imate][chunkInSizeBytesTotal[imate]]='\n';//extra empty line at the end of the chunks

            if (P.runThreadN>1) pthread_mutex_unlock(&g_threadChunks.mutexInRead);

        } else {//read from one file per thread
            noReadsLeft=true;
            for (uint imate=0; imate<P.readNmates; imate++) {
                RA->chunkOutFilterBySJoutFiles[imate].flush();
                RA->chunkOutFilterBySJoutFiles[imate].seekg(0,ios::beg);
                RA->readInStream[imate]=& RA->chunkOutFilterBySJoutFiles[imate];
            };
        };

        mapChunk();

        if (iThread==0 && P.runThreadN>1 && P.outSAMorder=="PairedKeepInputOrder") {//concatenate Aligned.* files
            chunkFilesCat(P.inOut->outSAM, P.outFileTmp + "/Aligned.out.sam.chunk", g_threadChunks.chunkOutN);
        };

    };//cycle over input chunks

    if (P.outFilterBySJoutStage!=1 && RA->iRead>0) {//not the first stage of the 2-stage mapping
        if (P.outBAMunsorted) chunkOutBAMunsorted->unsortedFlush();
        if (P.outBAMcoord) chunkOutBAMcoord->coordFlush();
        if (chunkOutBAMquant!=NULL) chunkOutBAMquant->unsortedFlush();

        //the thread is finished mapping reads, concatenate the temp files into output files
        if (P.pCh.segmentMin>0) {
            chunkFstreamCat (RA->chunkOutChimSAM, P.inOut->outChimSAM, P.runThreadN>1, g_threadChunks.mutexOutChimSAM);
            chunkFstreamCat (*RA->chunkOutChimJunction, P.inOut->outChimJunction, P.runThreadN>1, g_threadChunks.mutexOutChimJunction);
        };
        if (P.outReadsUnmapped=="Fastx" ) {
            if (P.runThreadN>1)
                pthread_mutex_lock(&g_threadChunks.mutexOutUnmappedFastx);

            for (uint ii=0;ii<P.readNmatesIn;ii++) {
                chunkFstreamCat (RA->chunkOutUnmappedReadsStream[ii],P.inOut->outUnmappedReadsStream[ii], false, g_threadChunks.mutexOutUnmappedFastx);
            };

            if (P.runThreadN>1)
                pthread_mutex_unlock(&g_threadChunks.mutexOutUnmappedFastx);
        };
    };
    if (P.runThreadN>1) pthread_mutex_lock(&g_threadChunks.mutexLogMain);
    P.inOut->logMain << "Completed: thread #" <<iThread <<endl;
    if (P.runThreadN>1) pthread_mutex_unlock(&g_threadChunks.mutexLogMain);
};

inline uint64 fastqReadOneLine(ifstream &streamIn, char *arrIn)
{
    uint64 lenIn;
    streamIn.getline(arrIn, DEF_readNameSeqLengthMax+1 );
    lenIn = streamIn.gcount(); //=seqLength+1: includes \0 but not \n. We will replace \0 with \n
    
    if ( int(arrIn[lenIn-2]) < 33 ) {//remove control char at the end if present
        --lenIn;
    };
    
    arrIn[lenIn-1]='\n'; //replace \0 with \n
    return lenIn;
};

inline void removeStringEndControl(string &str)
{//removes control character (including space) from the end of the string
    if (int(str.back())<33)
        str.pop_back();
};
