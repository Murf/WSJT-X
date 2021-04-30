subroutine q65b(nutc,fcenter,nfcal,nfsample,ikhz,mousedf,ntol,xpol,  &
     mycall0,hiscall0,hisgrid,mode_q65)

  use q65_decode
  use wideband_sync
  use timer_module, only: timer

  parameter (MAXFFT1=5376000)              !56*96000
  parameter (MAXFFT2=336000)               !56*6000 (downsampled by 1/16)
  parameter (NMAX=60*12000)
  integer*2 iwave(60*12000)
  complex ca(MAXFFT1),cb(MAXFFT1)          !FFTs of raw x,y data
  complex cx(0:MAXFFT2-1),cy(0:MAXFFT2-1),cz(0:MAXFFT2)
  logical xpol
  integer ipk1(1)
  real*8 fcenter,freq0
  character*12 mycall0,hiscall0
  character*12 mycall,hiscall
  character*6 hisgrid
  character*4 grid4
  character*80 line
  character*80 wsjtx_dir
  common/cacb/ca,cb
  save

  open(9,file='wsjtx_dir.txt',status='old')
  read(9,'(a)') wsjtx_dir
  close(9)

  if(mycall0(1:1).ne.' ') mycall=mycall0
  if(hiscall0(1:1).ne.' ') hiscall=hiscall0
  if(hisgrid(1:4).ne.'    ') grid4=hisgrid(1:4)

! Find best frequency and  ipol from sync_dat, the "orange sync curve".
  ff=ikhz+0.001*(mousedf+1270.459)              !supposed freq of sync tone
  ifreq=nint(1000.0*(ff-nkhz_center+48)*32768.0/96000.0)  !Freq index into ss(4,322,32768)
  dff=96000.0/32768.0
  ia=nint(ifreq-ntol/dff)
  ib=nint(ifreq+ntol/dff)
  ipk1=maxloc(sync_dat(ia:ib,2))
  ipk=ia+ipk1(1)-1
  ipol=1
  if(xpol) ipol=nint(sync_dat(ipk,4))
  nhz=nint((ipk-ifreq)*dff)

  nfft1=MAXFFT1
  nfft2=MAXFFT2
  df=96000.0/NFFT1
  if(nfsample.eq.95238) then
     nfft1=5120000
     nfft2=322560
     df=96000.0/nfft1
  endif
  nh=nfft2/2
  ikhz0=nint(1000.0*(fcenter-int(fcenter)))
  k0=(1000*(ikhz-ikhz0+48.0) + 1270 - 1000 + nfcal + mousedf + nhz)/df
  if(k0.lt.nh .or. k0.gt.nfft1-nh) go to 900

  fac=1.0/nfft2
  cx(0:nh)=ca(k0:k0+nh)
  cx(nh+1:nfft2-1)=ca(k0-nh+1:k0-1)
  cx=fac*cx
  if(xpol) then
     cy(0:nh)=cb(k0:k0+nh)
     cy(nh+1:nfft2-1)=cb(k0-nh+1:k0-1)
     cy=fac*cy
  endif

! Here cx and cy (if xpol) are frequency-domain data around the selected
! QSO frequency, taken from the full-length FFT computed in filbig().
! Values for fsample, nfft1, nfft2, df, and the downsampled data rate
! are as follows:

!  fSample  nfft1       df        nfft2  fDownSampled
!    (Hz)              (Hz)                 (Hz)
!----------------------------------------------------
!   96000  5376000  0.017857143  336000   6000.000
!   95238  5120000  0.018601172  322560   5999.994

  if(ipol.eq.1) cz(0:MAXFFT2-1)=cx
  if(ipol.eq.2) cz(0:MAXFFT2-1)=0.707*(cx+cy)
  if(ipol.eq.3) cz(0:MAXFFT2-1)=cy
  if(ipol.eq.4) cz(0:MAXFFT2-1)=0.707*(cx-cy)
  cz(MAXFFT2)=0.
!Transform to time domain (real), fsample=12000 Hz
  call four2a(cz,2*nfft2,1,1,-1)
  do i=0,nfft2-1
     j=nfft2-1-i
     iwave(2*i+1)=nint(real(cz(j)))
     iwave(2*i+2)=nint(aimag(cz(j)))
  enddo
  iwave(2*nfft2+1:)=0
  nsubmode=mode_q65-1
  nfa=max(100,1000-ntol)
  nfb=min(4900,1000+ntol)
  newdat=1
  nagain=0
  nsnr0=-99             !Default snr for no decode

  call timer('mmdec   ',0)
  call map65_mmdec(nutc,iwave,nsubmode,nfa,nfb,1000+mousedf,ntol,newdat,nagain,  &
       mycall,hiscall,hisgrid)
  call timer('mmdec   ',1)


  nfreq=nfreq0 + nhz + mousedf - 1000
  freq0=144.0 + 0.001*ikhz
  if(nsnr0.gt.-99) then
!     write(71,3071) mousedf,ntol,ia,ib,ifreq,ipk,ikhz,nfreq,nsnr0,xdt0,freq0,trim(msg0)
!3071 format(9i6,f6.2,f9.3,1x,a)
!     write(72,3072) mousedf,nfa,nfb,nfreq,ifreq,ipk,ipk-ifreq,nhz,(ipk-ifreq)*dff
!3072 format(8i6,f7.1)
     write(line,1020) ikhz,nfreq,45*(ipol-1),nutc,xdt0,nsnr0,msg0(1:27),cq0
1020 format('!',i3.3,i5,i4,i6.4,f5.1,i5,' : ',a27,a3)
     write(*,1100) trim(line)
1100 format(a)

! Should write to lu 26 here, for Messages and Band Map windows ?
!     write(26,1014) freq0,nfreq0,0,0,0,xdt0,ipol0,0,                   &
!          nsnr0,nutc,msg40(1:22),' ',char(ichar('A') + mode_q65-1)
!1014 format(f8.3,i5,3i3,f5.1,i4,i3,i4,i5.4,4x,a22,2x,a1,3x,':',a1)

! Write to file map65_rx.log:
     write(21,1110)  freq0,nfreq,xdt0,45*(ipol-1),nsnr0,nutc,msg0(1:28),cq0
1110 format(f8.3,i5,f5.1,2i4,i5.4,2x,a28,': A',2x,a3)
  endif

900 close(13)
  close(17)
  write(*,1900)
1900 format('<DecodeFinished>')
  call flush(6)

  return
end subroutine q65b
