! Generate integral-flux comparison points with an external RADBELT source tree.
program ae8ap8_radbelt_points
    use radbelt_kinds_module, only: wp
    use trmfun_module, only: trm_type
    implicit none
    type(trm_type) :: models(2)
    real(wp), parameter :: energies(2,2) = reshape([0.5_wp, 1.0_wp, 10.0_wp, 30.0_wp], [2,2])
    real(wp) :: energy, flux
    integer :: j, k
    do j = 1, 2
        do k = 1, 2
            energy = energies(k,j)
            call models(j)%aep8(energy, 2.0_wp, 1.2_wp, 2*j-1, flux)
            write (*,'(i1,",",f5.1,",",es24.16)') 2*j-1, energy, flux
        end do
    end do
end program
