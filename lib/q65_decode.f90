module q65_decode

   type :: q65_decoder
      procedure(q65_decode_callback), pointer :: callback
   contains
      procedure :: decode
   end type q65_decoder

   abstract interface
      subroutine q65_decode_callback (this,nutc,sync,nsnr,dt,freq,    &
         decoded,nap,qual,ntrperiod,fmid,w50)
         import q65_decoder
         implicit none
         class(q65_decoder), intent(inout) :: this
         integer, intent(in) :: nutc
         real, intent(in) :: sync
         integer, intent(in) :: nsnr
         real, intent(in) :: dt
         real, intent(in) :: freq
         character(len=37), intent(in) :: decoded
         integer, intent(in) :: nap
         real, intent(in) :: qual
         integer, intent(in) :: ntrperiod
         real, intent(in) :: fmid
         real, intent(in) :: w50
      end subroutine q65_decode_callback
   end interface

contains

  subroutine decode(this,callback,iwave,nutc,ntrperiod,nsubmode,nfqso,   &
       ntol,ndepth,mycall,hiscall,hisgrid)

! Decodes Q65 signals
! Input:  iwave            Raw data, i*2
!         nutc             UTC for time-tagging the decode
!         ntrperiod        T/R sequence length (s)
!         nsubmode         Tone-spacing indicator, 0-4 for A-E
!         nfqso            Target signal frequency (Hz)
!         ntol             Search range around nfqso (Hz)
!         ndepth           Optional decoding level (???)
! Output: sent to the callback routine for display to user

    use timer_module, only: timer
    use packjt
    use, intrinsic :: iso_c_binding
    parameter (NMAX=300*12000)            !Max TRperiod is 300 s
    class(q65_decoder), intent(inout) :: this
    procedure(q65_decode_callback) :: callback
    character(len=12) :: mycall, hiscall  !Used for AP decoding
    character(len=6) :: hisgrid
    character*37 decoded                  !Decoded message
    integer*2 iwave(NMAX)                 !Raw data
    real, allocatable :: dd(:)            !Raw data
    integer dat4(12)                      !Decoded message as 12 6-bit integers
    logical ltext
    complex, allocatable :: c00(:)        !Analytic signal, 6000 Sa/s
    complex, allocatable :: c0(:)         !Analytic signal, 6000 Sa/s
    data nc1z/-1/,nc2z/-1/,ng2z/-1/,maxaptypez/-1/,nsubmodez/-1/
    save nc1z,nc2z,ng2z,maxaptypez,nsubmodez

    mode65=2**nsubmode
    nfft1=ntrperiod*12000
    nfft2=ntrperiod*6000
    allocate(dd(NMAX))
    allocate (c00(0:nfft1-1))
    allocate (c0(0:nfft1-1))

    if(ntrperiod.eq.15) then
       nsps=1800
    else if(ntrperiod.eq.30) then
       nsps=3600
    else if(ntrperiod.eq.60) then
       nsps=7680
    else if(ntrperiod.eq.120) then
       nsps=16000
    else if(ntrperiod.eq.300) then
       nsps=41472
    else
       stop 'Invalid TR period'
    endif
    npts=ntrperiod*12000
    baud=12000.0/nsps
    df1=12000.0/nfft1
    this%callback => callback    
    if(nutc.eq.-999) print*,lapdx,nfa,nfb,nfqso  !Silence warning

! Prime the QRA decoder for possible use of AP
    call packcall(mycall(1:6),nc1,ltext)
    call packcall(hiscall(1:6),nc2,ltext)
    call packgrid(hisgrid(1:4),ng2,ltext)
    b90=20.0                 !8 to 25 is OK; not very critical
    nFadingModel=1

! AP control could be done differently, but this works well:
    maxaptype=0
!    if(ndepth.eq.2) maxaptype=3
!    if(ndepth.eq.3) maxaptype=5
    if(ndepth.ge.2) maxaptype=5       !###
    minsync=-2
    call qra_params(ndepth,maxaptype,idfmax,idtmax,ibwmin,ibwmax,maxdist)

    if(nc1.ne.nc1z .or. nc2.ne.nc2z .or. ng2.ne.ng2z .or.            &
         maxaptype.ne.maxaptypez) then
       do naptype=0,maxaptype
          if(naptype.eq.2 .and. maxaptype.eq.4) cycle
          call qra64_dec(s3dummy,nc1,nc2,ng2,naptype,1,nSubmode,b90,      &
               nFadingModel,dat4,snr2,irc)
       enddo
       nc1z=nc1
       nc2z=nc2
       ng2z=ng2
       maxaptypez=maxaptype
    endif
    naptype=maxaptype

    call timer('sync_q65',0)
    call sync_q65(iwave,ntrperiod*12000,mode65,nsps,nfqso,ntol,xdt,f0,snr1)
    call timer('sync_q65',1)

    irc=-1
    if(snr1.ge.2.5) then
       jpk0=(xdt+1.0)*6000                      !###
       if(ntrperiod.le.30) jpk0=(xdt+0.5)*6000  !###
       if(jpk0.lt.0) jpk0=0
       fac=1.0/32767.0
       dd=fac*iwave
       nmode=65
       call ana64(dd,npts,c00)
       call timer('qraloops',0)
       call qra_loops(c00,npts/2,nsps/2,nmode,mode65,nsubmode,nFadingModel,  &
            ndepth,nc1,nc2,ng2,naptype,jpk0,xdt,f0,width,snr2,irc,dat4)
       call timer('qraloops',1)
       snr2=snr2 + db(6912.0/nsps)
    endif
    decoded='                                     '
    if(irc.ge.0) then
       call unpackmsg(dat4,decoded)               !Unpack the user message
       call fmtmsg(decoded,iz)
       if(index(decoded,"000AAA ").ge.1) then
! Suppress a certain type of garbage decode.
          decoded='                      '
          irc=-1
       endif
       nsnr=nint(snr2)
       call this%callback(nutc,sync,nsnr,xdt,f0,decoded,              &
            irc,qual,ntrperiod,fmid,w50)
    else
       nsnr=db(snr1) - 32.0
!### TEMPORARY? ###       
       call this%callback(nutc,sync,nsnr,xdt,f0,decoded,              &
            irc,qual,ntrperiod,fmid,w50)
!###
    endif

!    write(61,3061) nutc,irc,xdt,f0,snr1,snr2,trim(decoded)
!3061 format(i6.6,i4,4f10.2,2x,a)

    return
  end subroutine decode

end module q65_decode
