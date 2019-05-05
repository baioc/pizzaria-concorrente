#!/bin/bash

INE5410_INFO=1

make

function exemplo() {
	./program 2 2 40 3 8 40 10
}

function mini_forno() {
	./program 2 10 40 40 40 40
}

function greve_pizlos() {
	./program 4 2 40 40 40 40
}

function inflacao() {
	./program 10 10 10 10 40 40
}

function greve_garcons() {
	./program 10 10 40 2 40 40
}

function escassez() {
	./program 10 10 40 40 3 40
}

$1
