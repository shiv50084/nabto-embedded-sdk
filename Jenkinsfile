
pipeline {
    agent none

    stages {
        stage('Build') {
            parallel {
                stage('Build linux amd64') {
                    agent {
                        dockerfile {
                            filename 'linux64.dockerfile'
                            dir 'build/linux'
                        }
                    }
                    environment {
                        releaseDir = "linux-release"
                        srcDir = pwd()
                    }

                    steps {
                        checkout scm
                        dir('build-amd64') {
                            sh "cmake -DCMAKE_INSTALL_PREFIX=${WORKSPACE}/${releaseDir} -DCMAKE_BUILD_TYPE=Release ${srcDir}"
                            sh "make -j"
                            sh "make install"
                        }
                        stash name: "linux-release", includes: "linux-release/**"
                    }
                }
                stage('Build linux armhf') {
                    agent {
                        dockerfile {
                            filename 'armhf.dockerfile'
                            dir 'build/linux'
                        }
                    }
                    environment {
                        CC="arm-linux-gnueabihf-gcc-8"
                        CXX="arm-linux-gnueabihf-g++-8"
                        releaseDir = "linux-armhf-release"
                        srcDir = pwd()

                    }
                    steps {
                        checkout scm
                        dir('build-armhf') {
                            sh "cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$WORKSPACE/${releaseDir} ${srcDir}"
                            sh "make -j"
                            sh "make install"
                        }
                        stash name: "${releaseDir}", includes: "${releaseDir}/**"
                    }
                }
                stage('Build on mac') {
                    agent {
                        label "mac"
                    }
                    environment {
                        PATH = "/usr/local/bin:$PATH"
                        releaseDir = "mac-release"
                        srcDir = pwd()
                    }
                    steps {
                        checkout scm
                        dir('build-mac') {
                            sh "cmake -DCMAKE_INSTALL_PREFIX=${WORKSPACE}/${releaseDir} -DCMAKE_BUILD_TYPE=Release ${srcDir}"
                            sh "make -j"
                            sh "make install"
                        }
                        stash name: "${releaseDir}", includes: "${releaseDir}/**"
                    }
                }
            }
        }
        stage('Test') {
            parallel {
                stage('Test on linux') {
                    agent {
                        dockerfile {
                            filename 'linux64.dockerfile'
                            dir 'build/linux'
                        }
                    }
                    steps {
                        dir ('test-dir') {
                            unstash "linux-release"
                            sh "./linux-release/bin/unit_test"
                            sh "./linux-release/bin/embedded_unit_test"
                        }
                    }
                }
                stage('Test on mac') {
                    agent {
                        label "mac"
                    }
                    steps {
                        dir ('test-dir') {
                            unstash "mac-release"
                            sh "./mac-release/bin/embedded_unit_test"
                        }
                    }
                }
            }
        }
        stage('Deploy') {
        // TODO
            agent {
                label "linux"
            }
            steps {
                dir('files') {
                    unstash "linux-release"
                    unstash "linux-armhf-release"
                    unstash "mac-release"
                }
            }
        }
    }
}
