subroutine osd300(llr,norder,decoded,niterations,cw)
!
! An ordered-statistics decoder based on ideas from: 
! "Soft-decision decoding of linear block codes based on ordered statistics,"
! by Marc P. C. Fossorier and Shu Lin, 
! IEEE Trans Inf Theory, Vol 41, No 5, Sep 1995
! 

include "ldpc_300_60_params.f90"

integer*1 gen(K,N)
integer*1 genmrb(K,N)
integer*1 temp(K),m0(K),me(K)
integer indices(N)
integer*1 codeword(N),cw(N),hdec(N)
integer*1 decoded(K)
integer indx(N)
real llr(N),rx(N),absrx(N)
logical first
data first/.true./

save first,gen

if( first ) then ! fill the generator matrix
  gen=0
  do i=1,M
    do j=1,15
      read(g(i)(j:j),"(Z1)") istr
        do jj=1, 4 
          irow=(j-1)*4+jj
          if( btest(istr,4-jj) ) gen(irow,i)=1
        enddo
    enddo
  enddo
  do irow=1,K
    gen(irow,M+irow)=1
  enddo
first=.false.
endif

! re-order received vector to place systematic msg bits at the end
rx=llr(colorder+1) 

! hard decode the received word
hdec=0            
where(rx .ge. 0) hdec=1

! use magnitude of received symbols as a measure of reliability.
absrx=abs(rx) 
call indexx(absrx,N,indx)  
! re-order the columns of the generator matrix in order of increasing reliability.
do i=1,N
  genmrb(1:K,N+1-i)=gen(1:K,indx(N+1-i))
enddo

! do gaussian elimination to create a generator matrix with the most reliable
! received bits as the systematic bits. if it happens that the K most reliable
! bits are not independent, then we will encounter a zero pivot, in that case
! we dip into the less reliable bits to find K independent MRBs.
! the "indices" array will track any column reordering that is done as part
! of the gaussian elimination.
do i=1,N
  indices(i)=indx(i)
enddo
do id=1,K ! diagonal element indices 
  do ic=id,K+20  ! The 20 is ad hoc - beware
    icol=N-K+ic
    if( icol .gt. N ) icol=M+1-(icol-N)
    iflag=0
    if( genmrb(id,icol) .eq. 1 ) then
      iflag=1
      if( icol-M .ne. id ) then ! reorder column
        temp(1:K)=genmrb(1:K,M+id)
        genmrb(1:K,M+id)=genmrb(1:K,icol)
        genmrb(1:K,icol)=temp(1:K) 
        itmp=indices(M+id)
        indices(M+id)=indices(icol)
        indices(icol)=itmp
      endif
      do ii=1,K
        if( ii .ne. id .and. genmrb(ii,N-K+id) .eq. 1 ) then
          genmrb(ii,1:N)=mod(genmrb(ii,1:N)+genmrb(id,1:N),2)
        endif
      enddo
      exit
    endif
  enddo
enddo

! now, use the indices of the K MRB bits to find the hard-decisions
! for those bits. the resulting message is encoded to find the 
! zero'th order codeword estimate (assuming no errors in the MRB).
m0=0
where (rx(indices(M+1:N)).ge.0.0) m0=1

! the MRB should have only a few errors. Try various error patterns,
! re-encode each errored version of the MRBs, re-order the resulting codeword
! and compare with the original received vector. Keep the best codeword.
nhardmin=N
corrmax=-1.0e32
j0=0
j1=0
j2=0
j3=0
if( norder.ge.4 ) j0=K
if( norder.ge.3 ) j1=K
if( norder.ge.2 ) j2=K
if( norder.ge.1 ) j3=K
do i1=0,j0
  do i2=i1,j1
    do i3=i2,j2
      do i4=i3,j3
        me=m0
        if( i1 .ne. 0 ) me(i1)=1-me(i1)
        if( i2 .ne. 0 ) me(i2)=1-me(i2)
        if( i3 .ne. 0 ) me(i3)=1-me(i3)
        if( i4 .ne. 0 ) me(i4)=1-me(i4)

! me is the MRB message + error pattern 
! use the modified generator matrix to encode this message, 
! producing a codeword that will be tested against the received vector
        do i=1,N 
          nsum=sum(iand(me,genmrb(1:K,i)))
          codeword(i)=mod(nsum,2)
        enddo
! undo the index permutations to put the "real" message bits at the end
        codeword(indices)=codeword
        nhard=count(codeword .ne. hdec)
!        corr=sum(codeword*rx)  ! to save time use nhard to pick best codeword
        if( nhard .lt. nhardmin ) then
!         if( corr .gt. corrmax ) then
          cw=codeword
          nhardmin=nhard
!          corrmax=corr
          i1min=i1
          i2min=i2
          i3min=i3
          i4min=i4
          if( nhardmin .le. 85 ) goto 200 ! tune for each code 
        endif
      enddo
    enddo
  enddo 
enddo

200 decoded=cw(M+1:N)
niterations=-1
if( nhardmin .le. 90 ) niterations=1      ! tune for each code
return
end subroutine osd300
