#!/bin/bash

make

function exemplo() {
	INE5410_INFO=$1 ./program 2 2 40 3 8 40 10
}

function mini_forno() {
	INE5410_INFO=$1 ./program 2 10 40 40 40 40
}

function greve_pizlos() {
	INE5410_INFO=$1 ./program 4 2 40 40 40 40
}

function inflacao() {
	INE5410_INFO=$1 ./program 10 10 10 10 40 40
}

function greve_garcons() {
	INE5410_INFO=$1 ./program 10 10 40 2 40 40
}

function escassez() {
	INE5410_INFO=$1 ./program 10 10 40 40 3 40
}

$1 $2 # chama funcao de teste pelo nome dado em arg[1]
